#!/usr/bin/env python
from __future__ import print_function
"""
Example implementation of Anki Victor Update engine.
"""
__author__ = "Daniel Casner <daniel@anki.com>"

import sys
import os
import urllib2
import subprocess
import tarfile
import zlib
import shutil
import ConfigParser
from select import select
from hashlib import sha256
from collections import OrderedDict
from fcntl import fcntl, F_GETFL, F_SETFL

sys.path.append("/anki/lib")
import update_payload

BOOT_DEVICE = "/dev/block/bootdevice/by-name"
STATUS_DIR = "/run/update-engine"
MOUNT_POINT = "/mnt/sdcard"
ANKI_REV_FILE = "/anki/etc/revision"
ANKI_VER_FILE = "/anki/etc/version"
EXPECTED_DOWNLOAD_SIZE_FILE = os.path.join(STATUS_DIR, "expected-download-size")
EXPECTED_WRITE_SIZE_FILE = os.path.join(STATUS_DIR, "expected-size")
PROGRESS_FILE = os.path.join(STATUS_DIR, "progress")
ERROR_FILE = os.path.join(STATUS_DIR, "error")
DONE_FILE = os.path.join(STATUS_DIR, "done")
MANIFEST_FILE = os.path.join(STATUS_DIR, "manifest.ini")
MANIFEST_SIG = os.path.join(STATUS_DIR, "manifest.sha256")
BOOT_STAGING = os.path.join(STATUS_DIR, "boot.img")
DELTA_STAGING = os.path.join(STATUS_DIR, "delta.bin")
OTA_PUB_KEY = "/anki/etc/ota.pub"
OTA_ENC_PASSWORD = "/anki/etc/ota.pas"
HTTP_BLOCK_SIZE = 1024*2  # Tuned to what seems to work best with DD_BLOCK_SIZE
DD_BLOCK_SIZE = HTTP_BLOCK_SIZE*1024
SUPPORTED_MANIFEST_VERSIONS = ["0.9.2", "0.9.3", "0.9.4", "0.9.5", "0.9.5"]

DEBUG = False


def make_blocking(pipe, blocking):
    "Set a filehandle to blocking or not"
    fd = pipe.fileno()
    if not blocking:
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) | os.O_NONBLOCK)  # set O_NONBLOCK
    else:
        fcntl(fd, F_SETFL, fcntl(fd, F_GETFL) & ~os.O_NONBLOCK)  # clear it


def safe_delete(name):
    "Delete a filesystem path name without error"
    if os.path.isfile(name):
        os.remove(name)
    elif os.path.isdir(name):
        shutil.rmtree(name)


def clear_status():
    "Clear everything out of the status directory"
    if os.path.isdir(STATUS_DIR):
        for filename in os.listdir(STATUS_DIR):
            os.remove(os.path.join(STATUS_DIR, filename))


def write_status(file_name, status):
    "Simple function to (over)write a file with a status"
    with open(file_name, "w") as output:
        output.write(str(status))
        output.truncate()


def die(code, text):
    "Write out an error string and exit with given status code"
    write_status(ERROR_FILE, text)
    if DEBUG:
        sys.stderr.write(str(text))
        sys.stderr.write(os.linesep)
    exit(code)


def open_slot(partition, slot, mode):
    "Opens a partition slot"
    if slot == "f":
        assert mode == "r"  # No writing to F slot
        if partition == "system":
            label = "recoveryfs"
        elif partition == "boot":
            label = "recovery"
        else:
            raise ValueError("Unknown partition")
    else:
        label = partition + "_" + slot
    return open(os.path.join(BOOT_DEVICE, label), mode + "b")


def zero_slot(target_slot):
    "Writes zeros to the first block of the destination slot boot and system to ensure they aren't booted"
    assert target_slot == 'a' or target_slot == 'b'  # Make sure we don't zero f
    zeroblock = b"\x00"*DD_BLOCK_SIZE
    open_slot("boot", target_slot, "w").write(zeroblock)
    open_slot("system", target_slot, "w").write(zeroblock)


def call(*args):
    "Simple wrapper arround subprocess.call to make ret=0 -> True"
    return subprocess.call(*args) == 0


def verify_signature(file_path_name, sig_path_name, public_key):
    "Verify the signature of a file based on a signature file and a public key with openssl"
    openssl = subprocess.Popen(["/usr/bin/openssl",
                                "dgst",
                                "-sha256",
                                "-verify",
                                public_key,
                                "-signature",
                                sig_path_name,
                                file_path_name],
                               shell=False,
                               stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE)
    ret_code = openssl.wait()
    openssl_out, openssl_err = openssl.communicate()
    return ret_code == 0, ret_code, openssl_out, openssl_err


def get_prop(property_name):
    "Gets a value from the property server via subprocess"
    getprop = subprocess.Popen(["/usr/bin/getprop", property_name], shell=False, stdout=subprocess.PIPE)
    if getprop.wait() == 0:
        return getprop.communicate()[0].strip()
    return None


def get_cmdline():
    "Returns /proc/cmdline arguments as a dict"
    cmdline = open("/proc/cmdline", "r").read()
    ret = {}
    for arg in cmdline.split(' '):
        try:
            key, val = arg.split('=')
        except ValueError:
            ret[arg] = None
        else:
            if val.isdigit():
                val = int(val)
            ret[key] = val
    return ret


def get_slot(kernel_command_line):
    "Get the current and target slots from the kernel commanlines"
    suffix = kernel_command_line.get("androidboot.slot_suffix", '_f')
    if suffix == '_a':
        return 'a', 'b'
    elif suffix == '_b':
        return 'b', 'a'
    else:
        return 'f', 'a'


def get_manifest(fileobj):
    "Returns config parsed from INI file in filelike object"
    config = ConfigParser.ConfigParser({'encryption': '0', 'cache': str(5*1024*1024)})
    config.readfp(fileobj)
    return config


class StreamDecompressor(object):
    "An object wrapper for handling possibly encrypted, compressed files"

    def __init__(self, src_file, encryption, compression, expected_size, do_sha=False):
        "Sets up the decompressor with it's subprocess, pipes etc."
        self.fileobj = src_file
        self.len = expected_size
        self.pos = 0
        self.sum = sha256() if do_sha else None
        cmds = []
        if encryption == 1:
            cmds.append("openssl enc -d -aes-256-ctr -pass file:{0}".format(OTA_ENC_PASSWORD))
        elif encryption != 0:
            die(210, "Unsupported encryption scheme {}".format(encryption))
        if compression == 'gz':
            cmds.append("gunzip")
        elif compression:
            die(205, "Unsupported compression scheme {}".format(compression))
        if cmds:
            self.proc = subprocess.Popen(" | ".join(cmds), shell=True,
                                         stdin=subprocess.PIPE,
                                         stdout=subprocess.PIPE,
                                         stderr=sys.stderr)
            make_blocking(self.proc.stdout, False)
        else:
            die(201, "Unhandled section format for expansion")

    def __del__(self):
        "Ensure all subprocesses are closed when class goes away"
        if self.proc.poll() is None:
            self.proc.kill()

    def read(self, length=-1):
        """Pumps the input and reads output"""
        block = b""
        while (length < 0 or len(block) < length) and (self.pos + len(block)) < self.len:
            rlist, wlist, xlist = select((self.proc.stdout,),
                                         (self.proc.stdin,),
                                         (self.proc.stdout, self.proc.stdin))
            if xlist:
                die(212, "Decompressor subprocess exceptional status")
            if self.proc.stdin in wlist:
                curl = self.fileobj.read(HTTP_BLOCK_SIZE)
                if len(curl) == HTTP_BLOCK_SIZE:
                    self.proc.stdin.write(curl)
                else:  # End the communication
                    block += self.proc.communicate(curl)[0]
                    break
            if self.proc.stdout in rlist:
                read_len = length - len(block) if length >= 0 else length
                block += self.proc.stdout.read(read_len)
        if self.sum:
            self.sum.update(block)
        self.pos += len(block)
        return block

    def digest(self):
        "Calculate the hexdigest if shasum has been being calculated"
        return self.sum.hexdigest() if self.sum else None

    def tell(self):
        "Return the number of bytes output so far"
        return self.pos

    def read_to_file(self, out_fh, block_size=DD_BLOCK_SIZE, progress_callback=None):
        "Read the entire contents to a given file"
        while self.pos < self.len:
            out_fh.write(self.read(block_size))
            if callable(progress_callback):
                progress_callback(self.pos)


def open_url_stream(url):
    "Open a URL as a filelike stream"
    try:
        assert url.startswith("http")  # Accepts http and https but not ftp or file
        os_version = get_prop("ro.anki.version")
        victor_version = get_prop("ro.anki.victor.version")
        if '?' in url:  # Already has a querry string
            if not url.endswith('?'):
                url += '&'
        else:
            url += '?'
        url += "emresn={0:s}&ankiversion={1:s}&victorversion={2:s}".format(
                get_prop("ro.serialno"),
                os_version,
                victor_version)
        request = urllib2.Request(url)
        opener = urllib2.build_opener()
        opener.addheaders = opener.addheaders = [('User-Agent', 'Victor/{0:s}'.format(os_version))]
        return opener.open(request)
    except Exception as e:
        die(203, "Failed to open URL: " + str(e))


def make_tar_stream(fileobj, open_mode="r|"):
    "Converts a file like object into a streaming tar object"
    try:
        return tarfile.open(mode=open_mode, fileobj=fileobj)
    except Exception as e:
        die(204, "Couldn't open contents as tar file " + str(e))


class ShaFile(object):
    "A fileobject wrapper that calculates a sha256 digest as it's processed"

    def __init__(self, fileobj):
        "Setup the wrapper"
        self.fileobj = fileobj
        self.sum = sha256()
        self.len = 0

    def read(self, *args):
        "Read function wrapper which adds calculates the shasum as it goes"
        block = self.fileobj.read(*args)
        self.sum.update(block)
        self.len += len(block)
        return block

    def digest(self):
        "Calculate the hexdigest"
        return self.sum.hexdigest()

    def bytes(self):
        "Return the total bytes read from the file"
        return self.len


class CacheStream(object):
    "A fileobject wrapper for streams which caches the first N bytes in memory allowing seeking back to them."

    def __init__(self, fileobj, cache_first, progress_callback=None):
        "Sets up the cachestream caching the first N bytes"
        self.src_file = fileobj
        self.cache = self.src_file.read(cache_first)
        self.fetched = len(self.cache)
        self.pos = 0
        self.progress = 0
        self.progress_callback = progress_callback

    def _update_pos(self, new_pos):
        "Update the position and track progress"
        self.pos = new_pos
        if new_pos > self.progress:
            self.progress = new_pos
            if callable(self.progress_callback):
                self.progress_callback(self.progress)

    def seek(self, offset):
        "Seek on the file if possible"
        if offset < len(self.cache):
            self._update_pos(offset)
        elif offset == self.fetched:
            self._update_pos(self.fetched)
        elif offset > self.fetched:
            _ = self.src_file.read(offset - self.fetched)
            self.fetched = offset
            self._update_pos(offset)
        else:
            raise IOError("Invalid seek on CacheStream({}): {}".format(len(self.cache), offset))
        return self.pos

    def read(self, length):
        "Read the given file"
        if self.pos + length <= len(self.cache):
            old_pos = self.pos
            self._update_pos(self.pos + length)
            return self.cache[old_pos:self.pos]
        elif self.pos < len(self.cache):
            if self.fetched == len(self.cache):
                need_to_read = self.pos + length - self.fetched
                read = self.src_file.read(need_to_read)
                self.fetched += len(read)
                self._update_pos(self.fetched)
                return self.cache[self.pos:] + read
            else:
                raise IOError("Invalid read on CacheStream({}): pos={}, length={}".format(len(self.cache),
                                                                                          self.pos,
                                                                                          length))
        elif self.pos == self.fetched:
            read = self.src_file.read(length)
            self.fetched += len(read)
            self._update_pos(self.fetched)
            return read
        else:
            raise IOError("Invalid read on CacheStream({}): pos={}, length={}".format(len(self.cache),
                                                                                      self.pos,
                                                                                      length))


def handle_boot_system(target_slot, manifest, tar_stream):
    "Process 0.9.2 format manifest files"
    total_size = manifest.getint("BOOT", "bytes") + manifest.getint("SYSTEM", "bytes")
    write_status(EXPECTED_WRITE_SIZE_FILE, total_size)
    written_size = 0
    write_status(PROGRESS_FILE, written_size)

    def progress_update(progress):
        "Update progress while writing to slots"
        write_status(PROGRESS_FILE, written_size + progress)
        if DEBUG:
            sys.stdout.write("{0:0.3f}\r".format(float(written_size+progress)/float(total_size)))
            sys.stdout.flush()

    # Extract boot image
    if DEBUG:
        print("Boot")
    boot_ti = tar_stream.next()
    if not boot_ti.name.endswith("boot.img.gz"):
        die(200, "Expected boot.img.gz to be next in tar but found \"{}\"".format(boot_ti.name))
    with open(BOOT_STAGING, "wb") as boot_fh:
        decompressor = StreamDecompressor(tar_stream.extractfile(boot_ti),
                                          manifest.getint("BOOT", "encryption"),
                                          manifest.get("BOOT", "compression"),
                                          manifest.getint("BOOT", "bytes"),
                                          True)
        decompressor.read_to_file(boot_fh, DD_BLOCK_SIZE, progress_update)
        # Verify boot hash
        if decompressor.digest() != manifest.get("BOOT", "sha256"):
            zero_slot(target_slot)
            die(209, "Boot image hash doesn't match signed manifest")
        written_size += decompressor.tell()

    # Extract system images
    if DEBUG:
        print("System")
    system_ti = tar_stream.next()
    if not system_ti.name.endswith("sysfs.img.gz"):
        die(200, "Expected sysfs.img.gz to be next in tar but found \"{}\"".format(system_ti.name))
    with open_slot("system", target_slot, "w") as system_slot:
        decompressor = StreamDecompressor(tar_stream.extractfile(system_ti),
                                          manifest.getint("SYSTEM", "encryption"),
                                          manifest.get("SYSTEM", "compression"),
                                          manifest.getint("SYSTEM", "bytes"),
                                          True)
        decompressor.read_to_file(system_slot, DD_BLOCK_SIZE, progress_update)
        if decompressor.digest() != manifest.get("SYSTEM", "sha256"):
            zero_slot(target_slot)
            die(209, "System image hash doesn't match signed manifest")
        written_size += decompressor.tell()
    # Actually write the boot image now
    with open(BOOT_STAGING, "rb") as src:
        with open_slot("boot", target_slot, "w") as dst:
            dst.write(src.read())


def copy_slot(partition, src_slot, dst_slot):
    "Copy the contents of a partition slot from one to the other"
    with open_slot(partition, src_slot, "r") as src:
        with open_slot(partition, dst_slot, "w") as dst:
            buffer = src.read(DD_BLOCK_SIZE)
            while len(buffer) == DD_BLOCK_SIZE:
                dst.write(buffer)
                buffer = src.read(DD_BLOCK_SIZE)
            dst.write(buffer)


def update_build_props(mount_point):
    "Updates (or creates) a property in the build.prop file specified"
    # Get the Anki Victor info
    victor_build_ver = open(os.path.join(mount_point, ANKI_VER_FILE), "r").read().strip()
    victor_build_rev = open(os.path.join(mount_point, ANKI_REV_FILE), "r").read().strip()
    build_prop_path_name = os.path.join(mount_point, "build.prop")
    # Get all the old OS properties
    props = OrderedDict()
    for key, value in [p.strip().split('=') for p in open(build_prop_path_name, "r").readlines()]:
        props[key] = value
    os_version = props['ro.anki.version']
    os_build_timestamp = props['ro.build.version.release']
    os_rev = ""  # TODO find this somewhere
    props["ro.revision"] = "anki-{VICTOR_BUILD_REV}_os-{REV}".format(VICTOR_BUILD_REV=victor_build_rev, REV=os_rev)
    props["ro.anki.victor.version"] = victor_build_ver
    version_id = "v{VICTOR_BUILD_VERSION}_os{OS_VERSION}".format(
        VICTOR_BUILD_VERSION=victor_build_ver,
        OS_VERSION=os_version
    )
    build_id = "v{VICTOR_BUILD_VERSION}{VICTOR_REV_TAG}_os{OS_VERSION}{REV_TAG}-{BUILD_TIMESTAMP}".format(
        VICTOR_BUILD_VERSION=victor_build_ver,
        VICTOR_REV_TAG="-" + victor_build_rev if victor_build_rev else "",
        OS_VERSION=os_version,
        REV_TAG="-" + os_rev if os_rev else "",
        BUILD_TIMESTAMP=os_build_timestamp
    )
    props["ro.build.fingerprint"] = build_id
    props["ro.build.id"] = build_id
    props["ro.build.display.id"] = version_id

    with open(build_prop_path_name, "w") as propfile:
        propfile.write("\n".join(["=".join(prop) for prop in props.items()]))
        propfile.write("\n")


def handle_delta(current_slot, target_slot, manifest, tar_stream):
    "Apply a delta update to the boot and system partitions"
    current_version = get_prop("ro.anki.version")
    delta_base_version = manifest.get("DELTA", "base_version")
    if current_version != delta_base_version:
        die(211, "My version is {} not {}".format(current_version, delta_base_version))
    write_status(EXPECTED_WRITE_SIZE_FILE, manifest.getint("DELTA", "bytes"))
    write_status(PROGRESS_FILE, 0)

    def progress_update(progress):
        "Update delta download progress"
        write_status(PROGRESS_FILE, progress)
        if DEBUG:
            sys.stdout.write("{0:0.3f}\r".format(float(progress)/float(manifest.getint("DELTA", "bytes"))))
            sys.stdout.flush()

    # Extract delta file
    if DEBUG:
        print("Delta")
    delta_ti = tar_stream.next()
    if not delta_ti.name.endswith("delta.bin.gz"):
        die(200,
            "Expected delta.bin.gz to be next in tar but found \"{}\""
            .format(delta_ti.name))
    decompressor = StreamDecompressor(tar_stream.extractfile(delta_ti),
                                      manifest.getint("DELTA", "encryption"),
                                      manifest.get("DELTA", "compression"),
                                      manifest.getint("DELTA", "bytes"),
                                      True)
    try:
        payload = update_payload.Payload(CacheStream(decompressor, manifest.getint("DELTA", "cache"), progress_update))
        payload.Init()
        payload.Apply(os.path.join(BOOT_DEVICE, "boot_" + target_slot),
                      os.path.join(BOOT_DEVICE, "system_" + target_slot),
                      os.path.join(BOOT_DEVICE, "boot_" + current_slot),
                      os.path.join(BOOT_DEVICE, "system_" + current_slot),
                      truncate_to_expected_size=False)
    except update_payload.PayloadError as pay_err:
        die(207, "Delta payload error: {!s}".format(pay_err))
    # Verify delta hash
    if decompressor.digest() != manifest.get("DELTA", "sha256"):
        zero_slot(target_slot)
        die(209, "delta.bin hash doesn't match manifest value")


def handle_anki(current_slot, target_slot, manifest, tar_stream):
    "Update the Anki folder only"
    write_status(EXPECTED_WRITE_SIZE_FILE, 4)  # We're faking progress here with just stages 0-N
    write_status(PROGRESS_FILE, 0)
    if DEBUG:
        print("Copying system from {} to {}".format(current_slot, target_slot))
    copy_slot("system", current_slot, target_slot)
    write_status(PROGRESS_FILE, 1)
    if DEBUG:
        print("Installing new Anki")
    if not call(["mount", os.path.join(BOOT_DEVICE, "system" + "_" + target_slot), MOUNT_POINT]):
        die(208, "Couldn't mount target system partition")
    try:
        anki_path = os.path.join(MOUNT_POINT, "anki")
        shutil.rmtree(anki_path)
        write_status(PROGRESS_FILE, 2)
        anki_ti = tar_stream.next()
        src_file = tar_stream.extractfile(anki_ti)
        if manifest.getint("ANKI", "encryption") != 0:
            die(210, "Encrypted Anki updates are not supported")
        else:
            sha_fh = ShaFile(src_file)
        anki_tar = make_tar_stream(sha_fh, "r|" + manifest.get("ANKI", "compression"))
        anki_tar.extractall(MOUNT_POINT)
        update_build_props(MOUNT_POINT)
    finally:
        call(["umount", MOUNT_POINT])
    write_status(PROGRESS_FILE, 3)
    # Verify anki tar hash
    if sha_fh.bytes() != manifest.getint("ANKI", "bytes"):
        zero_slot(target_slot)
        die(209, "Anki archive wrong size")
    if sha_fh.digest() != manifest.get("ANKI", "sha256"):
        zero_slot(target_slot)
        die(209, "Anki archive didn't match signed manifest")
    # Copy over the boot partition since we passed
    if DEBUG:
        print("Sig passed, installing kernel")
    copy_slot("boot", current_slot, target_slot)
    write_status(PROGRESS_FILE, 4)


def update_from_url(url):
    "Updates the inactive slot from the given URL"
    # Figure out slots
    cmdline = get_cmdline()
    current_slot, target_slot = get_slot(cmdline)
    if DEBUG:
        print("Target slot is", target_slot)
    # Make status directory
    if not os.path.isdir(STATUS_DIR):
        os.mkdir(STATUS_DIR)
    # Open URL as a tar stream
    stream = open_url_stream(url)
    content_length = stream.info().getheaders("Content-Length")[0]
    write_status(EXPECTED_DOWNLOAD_SIZE_FILE, content_length)
    with make_tar_stream(stream) as tar_stream:
        # Get the manifest
        if DEBUG:
            print("Manifest")
        manifest_ti = tar_stream.next()
        if not manifest_ti.name.endswith('manifest.ini'):
            die(200, "Expected manifest.ini at beginning of download, found \"{0.name}\"".format(manifest_ti))
        with open(MANIFEST_FILE, "wb") as manifest:
            manifest.write(tar_stream.extractfile(manifest_ti).read())
        manifest_sig_ti = tar_stream.next()
        if not manifest_sig_ti.name.endswith('manifest.sha256'):
            die(200, "Expected manifest signature after manifest.ini. Found \"{0.name}\"".format(manifest_sig_ti))
        with open(MANIFEST_SIG, "wb") as signature:
            signature.write(tar_stream.extractfile(manifest_sig_ti).read())
        verification_status = verify_signature(MANIFEST_FILE, MANIFEST_SIG, OTA_PUB_KEY)
        if not verification_status[0]:
            die(209,
                "Manifest failed signature validation, openssl returned {1:d} {2:s} {3:s}".format(*verification_status))
        # Manifest was signed correctly
        manifest = get_manifest(open(MANIFEST_FILE, "r"))
        # Inspect the manifest
        if manifest.get("META", "manifest_version") not in SUPPORTED_MANIFEST_VERSIONS:
            die(201, "Unexpected manifest version")
        if DEBUG:
            print("Updating to version {}".format(manifest.get("META", "update_version")))
        # Mark target unbootable
        if not call(['/bin/bootctl', current_slot, 'set_unbootable', target_slot]):
            die(202, "Could not mark target slot unbootable")
        zero_slot(target_slot)  # Make it doubly unbootable just in case
        num_images = manifest.getint("META", "num_images")
        if num_images == 2:
            if manifest.has_section("BOOT") and manifest.has_section("SYSTEM"):
                handle_boot_system(target_slot, manifest, tar_stream)
            else:
                die(201, "Two images specified but couldn't find boot or system")
        elif num_images == 1:
            if manifest.has_section("DELTA"):
                handle_delta(current_slot, target_slot, manifest, tar_stream)
            elif manifest.has_section("ANKI"):
                handle_anki(current_slot, target_slot, manifest, tar_stream)
            else:
                die(201, "One image specified but not DELTA or ANKI")
        else:
            die(201, "Unexpected manifest configuration")
    stream.close()
    # Ensure new images are synced to disk
    if not call(["/bin/sync"]):
        die(208, "Couldn't sync OS images to disk")
    # Mark the slot bootable now
    if not call(["/bin/bootctl", current_slot, "set_active", target_slot]):
        die(202, "Could not set target slot as active")
    write_status(DONE_FILE, 1)


if __name__ == '__main__':
    if len(sys.argv) == 1:  # Clear the output directory
        clear_status()
        exit(0)
    elif len(sys.argv) == 3 and sys.argv[2] == '-v':
        DEBUG = True

    if DEBUG:
        update_from_url(sys.argv[1])
    else:
        try:
            update_from_url(sys.argv[1])
        except zlib.error as decompressor_error:
            die(205, "Decompression error: " + str(decompressor_error))
        except IOError as io_error:
            die(208, "IO Error: " + str(io_error))
        except Exception as e:
            die(219, e)
        exit(0)
