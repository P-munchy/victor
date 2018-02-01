#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <stdatomic.h>
#include <pthread.h>

// linux kernel header provided for ION memory access
#include "linux/msm_ion.h"

#include "camera_client.h"
#include "log.h"

static char *cli_socket_path = "/tmp/cam_client0";
static char *srv_socket_path = "/var/run/mm-anki-camera/camera-server";

#define ANKI_CAMERA_MAX_PACKETS 8
#define ANKI_CAMERA_MSG_PAYLOAD_LEN 128
#define ANKI_CAMERA_MAX_FRAME_COUNT 5

static const uint64_t HEARTBEAT_INTERVAL_NS = 200000000;
static const uint64_t HEARTBEAT_INTERVAL_US = 200000;

#define NSEC_PER_SEC ((uint64_t)1000000000)
#define NSEC_PER_MSEC ((uint64_t)1000000)
#define NSEC_PER_USEC (1000)

//
// IPC Message Protocol
//
typedef enum {
  ANKI_CAMERA_MSG_C2S_HEARTBEAT,
  ANKI_CAMERA_MSG_C2S_CLIENT_REGISTER,
  ANKI_CAMERA_MSG_C2S_CLIENT_UNREGISTER,
  ANKI_CAMERA_MSG_C2S_START,
  ANKI_CAMERA_MSG_C2S_STOP,
  ANKI_CAMERA_MSG_C2S_PARAMS,
  ANKI_CAMERA_MSG_S2C_STATUS,
  ANKI_CAMERA_MSG_S2C_BUFFER,
  ANKI_CAMERA_MSG_S2C_HEARTBEAT,
} anki_camera_msg_id_t;

struct anki_camera_msg {
  anki_camera_msg_id_t msg_id;
  uint32_t version;
  uint32_t client_id;
  int fd;
  uint8_t payload[ANKI_CAMERA_MSG_PAYLOAD_LEN];
};

//
// ION memory management info
//
struct camera_capture_mem_info {
  int camera_capture_fd;
  int ion_fd;
  ion_user_handle_t ion_handle;
  uint32_t size;
  uint8_t *data;
};

//
// Internal Layout of shared camera capture memory
//
typedef struct {
  _Atomic uint32_t write_idx;
  _Atomic uint32_t frame_locks[ANKI_CAMERA_MAX_FRAME_COUNT];
} anki_camera_buf_lock_t;

typedef struct {
  uint8_t magic[4];
  anki_camera_buf_lock_t locks;
  uint32_t frame_count;
  uint32_t frame_size;
  uint32_t frame_offsets[ANKI_CAMERA_MAX_FRAME_COUNT];
  uint8_t data[0];
} anki_camera_buf_header_t;

//
// IPC Client State
//
struct client_ctx {
  pthread_t ipc_thread;

  int fd;
  int is_running;
  int request_close;
  int request_start;
  anki_camera_status_t status;

  struct camera_capture_mem_info camera_buf;
  uint64_t locked_slots[ANKI_CAMERA_MAX_FRAME_COUNT];

  uint32_t rx_cursor;
  struct anki_camera_msg rx_packets[ANKI_CAMERA_MAX_PACKETS];

  uint32_t tx_cursor;
  struct anki_camera_msg tx_packets[ANKI_CAMERA_MAX_PACKETS];
};

//
// Internal extended version of public struct
//
struct anki_camera_handle_private {
  int client_handle;
  uint32_t current_frame_id;

  // private
  struct client_ctx camera_client;
};

static ssize_t read_fd(int fd, void *ptr, size_t nbytes, int *recvfd)
{
  struct msghdr msg;
  struct iovec iov[1];
  ssize_t n;
  int newfd;

  union {
    struct cmsghdr cm;
    char control[CMSG_SPACE(sizeof(int))];
  } control_un;
  struct cmsghdr *cmptr;

  msg.msg_control = control_un.control;
  msg.msg_controllen = sizeof(control_un.control);

  msg.msg_name = NULL;
  msg.msg_namelen = 0;

  iov[0].iov_base = ptr;
  iov[0].iov_len = nbytes;
  msg.msg_iov = iov;
  msg.msg_iovlen = 1;

  if ((n = recvmsg(fd, &msg, 0)) <= 0)
  { return (n); }

  if ((cmptr = CMSG_FIRSTHDR(&msg)) != NULL &&
      cmptr->cmsg_len == CMSG_LEN(sizeof(int))) {
    if (cmptr->cmsg_level != SOL_SOCKET) {
      loge("%s: control level != SOL_SOCKET: %s", __func__, strerror(errno));
      return -1;
    }
    if (cmptr->cmsg_type != SCM_RIGHTS) {
      loge("%s: control type != SCM_RIGHTS: %s", __func__, strerror(errno));
      return -1;
    }
    *recvfd = *((int *)CMSG_DATA(cmptr));
  }
  else {
    *recvfd = -1; /* descriptor was not passed */
  }

  return (n);
}
/* end read_fd */

static int configure_socket(int socket)
{
  int flags = fcntl(socket, F_GETFL, 0);
  if (flags == -1) {
    loge("%s: configure socket: %s", __func__, strerror(errno));
    return -1;
  }

  flags |= O_NONBLOCK;
  flags = fcntl(socket, F_SETFL, flags);
  if (flags == -1) {
    loge("%s: configure nonblocking: %s", __func__, strerror(errno));
    return -1;
  }

  const int enable = 1;
  const int status = setsockopt(socket, SOL_SOCKET, SO_REUSEADDR, &enable, sizeof(enable));

  return status;
}

static void debug_dump_image_frame(uint8_t *frame, int width, int height, int bpp)
{
  char file_name[512];
  static int frame_idx = 0;
  const char *name = "anki_camera";
  const char *ext = "rgb";
  int file_fd;

  snprintf(file_name, sizeof(file_name), "/tmp/cc_%04d.%s", frame_idx++, ext);
  file_fd = open(file_name, O_RDWR | O_CREAT, 0777);
  if (file_fd < 0) {
    loge("%s: cannot open file %s \n", __func__, file_name);
  }
  else {
    write(file_fd, frame, width * height * bpp / 8);
  }

  close(file_fd);
  logi("%s: wrote %s\n", __func__, file_name);
}

static int socket_connect(int *out_fd)
{
  struct sockaddr_un caddr;
  struct sockaddr_un saddr;
  char buf[100];
  int fd = -1;
  int rc = 0;

  if ((fd = socket(AF_UNIX, SOCK_DGRAM, 0)) == -1) {
    loge("%s: socket error: %s", __func__, strerror(errno));
    return -1;
  }

  // configure non-blocking
  configure_socket(fd);

  // bind client socket
  memset(&caddr, 0, sizeof(caddr));
  caddr.sun_family = AF_UNIX;
  strncpy(caddr.sun_path, cli_socket_path, sizeof(caddr.sun_path) - 1);
  unlink(cli_socket_path);

  if (bind(fd, (struct sockaddr *)&caddr, sizeof(caddr)) == -1) {
    loge("%s: bind error: %s", __func__, strerror(errno));
    return -1;
  }

  // connect to server socket
  memset(&saddr, 0, sizeof(saddr));
  saddr.sun_family = AF_UNIX;
  strncpy(saddr.sun_path, srv_socket_path, sizeof(saddr.sun_path) - 1);

  if (connect(fd, (struct sockaddr *)&saddr, sizeof(saddr)) == -1) {
    loge("%s: connect error: %s", __func__, strerror(errno));
    return -1;
  }

  *out_fd = fd;
  return rc;
}

static int send_message(struct client_ctx *ctx, struct anki_camera_msg *msg)
{
  ssize_t bytes_sent = write(ctx->fd, msg, sizeof(struct anki_camera_msg));
  if (bytes_sent != sizeof(struct anki_camera_msg)) {
    loge("%s: write error: %zd %s\n", __func__, bytes_sent, strerror(errno));
    return -1;
  }

  return 0;
}

static int unmap_camera_capture_buf(struct client_ctx *ctx)
{
  struct camera_capture_mem_info *mem_info = &(ctx->camera_buf);

  int rc = 0;

  if ((mem_info->data != NULL) && (mem_info->camera_capture_fd > 0) && (mem_info->ion_handle > 0)) {
    rc = munmap(mem_info->data, mem_info->size);
  }

  if (rc == -1) {
    loge("%s: failed to unmap ION mem: %s", __func__, strerror(errno));
  }

  mem_info->data = NULL;
  mem_info->size = 0;

  if (mem_info->camera_capture_fd > 0) {
    close(mem_info->camera_capture_fd);
    mem_info->camera_capture_fd = -1;
  }

  if (mem_info->ion_fd > 0) {
    struct ion_handle_data handle_data;
    memset(&handle_data, 0, sizeof(handle_data));
    handle_data.handle = mem_info->ion_handle;
    rc = ioctl(mem_info->ion_fd, ION_IOC_FREE, &handle_data);
    if (rc != 0) {
      loge("%s: failed to free ION mem: %s", __func__, strerror(errno));
    }
    close(mem_info->ion_fd);
    mem_info->ion_fd = -1;
  }

  return rc;
}

static int mmap_camera_capture_buf(struct client_ctx *ctx)
{
  struct camera_capture_mem_info *mem_info = &(ctx->camera_buf);

  int main_ion_fd = open("/dev/ion", O_RDONLY);
  if (main_ion_fd <= 0) {
    loge("%s: Ion dev open failed: %s", __func__, strerror(errno));
    goto ION_OPEN_FAILED;
  }

  // ion_share - import shared fd
  int ret;
  struct ion_fd_data data = {
    .fd = mem_info->camera_capture_fd,
  };

  ret = ioctl(main_ion_fd, ION_IOC_IMPORT, &data);
  if (ret < 0) {
    loge("%s: Ion import failed: %s", __func__, strerror(errno));
    goto ION_IMPORT_FAILED;
  }

  struct ion_fd_data ion_info_fd;
  memset(&ion_info_fd, 0, sizeof(ion_info_fd));
  ion_info_fd.handle = data.handle;
  int rc = ioctl(main_ion_fd, ION_IOC_SHARE, &ion_info_fd);
  if (rc < 0) {
    loge("%s: ION map failed: %s\n", __func__, strerror(errno));
    goto ION_MAP_FAILED;
  }

  size_t buf_size = mem_info->size;
  size_t buf_size_align = (buf_size + 4095U) & (~4095U);
  assert(buf_size == buf_size_align);
  uint8_t *buf = mmap(NULL,
                      buf_size,
                      PROT_READ | PROT_WRITE,
                      MAP_SHARED,
                      ion_info_fd.fd,
                      0);

  if (buf == MAP_FAILED) {
    loge("%s: ION mmap failed: %s", __func__, strerror(errno));
    goto ION_MAP_FAILED;
  }

  mem_info->ion_fd = main_ion_fd;
  mem_info->camera_capture_fd = ion_info_fd.fd;
  mem_info->ion_handle = data.handle;
  mem_info->data = buf;

  return 0;

ION_MAP_FAILED: {
    struct ion_handle_data handle_data;
    memset(&handle_data, 0, sizeof(handle_data));
    handle_data.handle = ion_info_fd.handle;
    ioctl(main_ion_fd, ION_IOC_FREE, &handle_data);
  }
ION_IMPORT_FAILED: {
    close(main_ion_fd);
  }
ION_OPEN_FAILED:
  return -1;
}

//
// manage client slot->frame mapping
//

// entries in locked_slots[] are 64bits.
// We store the frame_id as a 32-bit value, with bit32 indicating occupancy.
// Empty entries are set to zero.
static const uint64_t LOCKED_FLAG = 0x100000000ULL;
static const uint64_t VALUE_MASK = 0x000000000ffffffffULL;

static int add_locked_slot(struct client_ctx *ctx, uint8_t slot, uint32_t frame_id)
{
  assert(slot < ANKI_CAMERA_MAX_FRAME_COUNT);
  int rc = -1;
  if (slot < ANKI_CAMERA_MAX_FRAME_COUNT) {
    uint64_t v = ctx->locked_slots[slot];
    if (v == 0) {
      ctx->locked_slots[slot] = (frame_id | LOCKED_FLAG);
      rc = 0;
    }
  }
  return rc;
}

static int get_locked_frame(struct client_ctx *ctx, uint32_t slot, uint32_t *out_frame_id)
{
  assert(slot < ANKI_CAMERA_MAX_FRAME_COUNT);
  int is_locked = -1;
  if (slot < ANKI_CAMERA_MAX_FRAME_COUNT) {
    uint64_t v = ctx->locked_slots[slot];
    if ((v & LOCKED_FLAG) == LOCKED_FLAG) {
      *out_frame_id = (uint32_t)(v & VALUE_MASK);
      is_locked = 0;
    }
  }
  return is_locked;
}

static int get_locked_slot(struct client_ctx *ctx, uint32_t frame_id, uint32_t *out_slot)
{
  int is_locked = -1;
  for (uint32_t slot = 0; slot < ANKI_CAMERA_MAX_FRAME_COUNT; ++slot) {
    uint64_t v = ctx->locked_slots[slot];
    if (v == (frame_id | LOCKED_FLAG)) {
      *out_slot = slot;
      is_locked = 0;
      break;
    }
  }
  return is_locked;
}

static int remove_locked_slot(struct client_ctx *ctx, uint32_t frame_id, uint32_t *out_slot)
{
  uint32_t slot;
  int rc = get_locked_slot(ctx, frame_id, &slot);
  if (rc == 0) {
    if (out_slot != NULL) {
      *out_slot = slot;
    }
    ctx->locked_slots[slot] = 0;
  }
  return rc;
}

static int write_outgoing_data(struct client_ctx *ctx)
{
  uint32_t i;
  int rc;
  uint32_t msg_count = ctx->tx_cursor;
  for (i = 0; i < msg_count; ++i) {
    struct anki_camera_msg *msg = &(ctx->tx_packets[i]);
    rc = send_message(ctx, msg);
    logv("%s: send msg %d", __func__, msg->msg_id);
    ctx->tx_cursor--;
    if (rc != 0) {
      break;
    }
  }
  return rc;
}

static int enqueue_message(struct client_ctx *ctx, anki_camera_msg_id_t msg_id)
{
  uint32_t cursor = ctx->tx_cursor;
  struct anki_camera_msg *msg = &ctx->tx_packets[cursor];
  msg->msg_id = msg_id;
  ctx->tx_cursor = cursor + 1;
  logv("%s: enqueue_message: %d", __func__, msg_id);
  return 0;
}

static int process_one_message(struct client_ctx *ctx, struct anki_camera_msg *msg)
{
  int rc = 0;
  const anki_camera_msg_id_t msg_id = msg->msg_id;
  switch (msg_id) {
  case ANKI_CAMERA_MSG_S2C_STATUS: {
    anki_camera_msg_id_t ack_msg_id = msg->payload[0];
    logv("%s: received STATUS ack: %d\n", __func__, ack_msg_id);
    switch (ack_msg_id) {
    case ANKI_CAMERA_MSG_C2S_CLIENT_REGISTER: {
      ctx->status = ANKI_CAMERA_STATUS_IDLE;
    }
    break;
    case ANKI_CAMERA_MSG_C2S_CLIENT_UNREGISTER: {
      ctx->status = ANKI_CAMERA_STATUS_OFFLINE;
    }
    break;
    case ANKI_CAMERA_MSG_C2S_START: {
      ctx->status = ANKI_CAMERA_STATUS_RUNNING;
    }
    break;
    case ANKI_CAMERA_MSG_C2S_STOP: {
      ctx->status = ANKI_CAMERA_STATUS_IDLE;
    }
    break;
    default:
      break;
    }
  }
  break;
  case ANKI_CAMERA_MSG_S2C_BUFFER: {
    // payload contains len
    uint32_t buffer_size;
    memcpy(&buffer_size, msg->payload, sizeof(buffer_size));
    logv("%s: received ANKI_CAMERA_MSG_S2C_BUFFER :: fd=%d size=%u\n",
         __func__, msg->fd, buffer_size);
    ctx->camera_buf.camera_capture_fd = msg->fd;
    ctx->camera_buf.size = buffer_size;
    rc = mmap_camera_capture_buf(ctx);
  }
  break;
  case ANKI_CAMERA_MSG_S2C_HEARTBEAT: {
    break;
  }
  break;
  default: {
    loge("%s: received unexpected message: %d\n", __func__, msg_id);
    rc = -1;
  }
  break;
  }
  return rc;
}

static int process_incoming_messages(struct client_ctx *ctx)
{
  uint32_t i;
  int rc;
  uint32_t msg_count = ctx->rx_cursor;
  for (i = 0; i < msg_count; ++i) {
    struct anki_camera_msg *msg = &(ctx->rx_packets[i]);
    rc = process_one_message(ctx, msg);
    ctx->rx_cursor--;
    if (rc != 0) {
      break;
    }
  }
  return rc;
}

static int read_incoming_data(struct client_ctx *ctx)
{
  // Read all available data
  int rc = -1;
  do {
    // Check for space to receive data
    if (ctx->rx_cursor == ANKI_CAMERA_MAX_PACKETS) {
      loge("%s: No more space, dropping packet", __func__);
      rc = -1;
      break;
    }

    // Prepare rx buffer
    struct anki_camera_msg *msg = &(ctx->rx_packets[ctx->rx_cursor]);
    memset(msg, 0, sizeof(struct anki_camera_msg));
    int recv_fd = -1;
    rc = read_fd(ctx->fd, msg, sizeof(struct anki_camera_msg), &recv_fd);
    if (rc < 0) {
      if (errno == EAGAIN || errno == EWOULDBLOCK) {
        // Expected normal case after reading all data
        rc = 0;
      }
      else {
        loge("%s: read failed: %s", __func__, strerror(errno));
        rc = -1; // indicate read failed
      }
    }
    else if (rc > 0) {
      // mark rx buffer slot as used
      ctx->rx_cursor++;

      if (recv_fd >= 0) {
        // If we received a file descriptor, store it
        msg->fd = recv_fd;
      }

      logv("%s: received msg:%d fd:%d\n", __func__, msg->msg_id, recv_fd);
    }
  }
  while (rc > 0);

  if (rc == 0) {
    process_incoming_messages(ctx);
  }

  return rc;
}

static int event_loop(struct client_ctx *ctx)
{
  struct timeval timeout;
  fd_set read_fds;
  fd_set write_fds;

  int fd = ctx->fd;
  int max_fd = fd;

  int rc = -1;
  do {
    FD_ZERO(&write_fds);
    FD_ZERO(&read_fds);
    FD_SET(fd, &read_fds);

    if (ctx->tx_cursor > 0) {
      FD_SET(ctx->fd, &write_fds);
    }

    timeout.tv_sec = 0;
    timeout.tv_usec = HEARTBEAT_INTERVAL_US;

    rc = select(max_fd + 1, &read_fds, &write_fds, NULL, &timeout);

    if (rc < 0) {
      loge("%s: select failed: %s", __func__, strerror(errno));
      break;
    }

    if (rc == 0) {
      // select timeout
      break;
    }

    if (rc > 0) {
      if (FD_ISSET(fd, &write_fds)) {
        logv("%s: write %d\n", __func__, rc);
        rc = write_outgoing_data(ctx);
        if (rc == -1) {
          break;
        }
      }
      if (FD_ISSET(fd, &read_fds)) {
        logv("%s: read %d\n", __func__, rc);
        rc = read_incoming_data(ctx);
        if (rc == -1) {
          break;
        }
      }
    }
  }
  while (ctx->is_running);

  return rc;
}

static void *camera_client_thread(void *camera_handle_ptr)
{
  logi("%s: start", __func__);
  struct anki_camera_handle_private *handle = (struct anki_camera_handle_private *)camera_handle_ptr;
  struct client_ctx *client = &handle->camera_client;

  client->status = ANKI_CAMERA_STATUS_IDLE;
  enqueue_message(client, ANKI_CAMERA_MSG_C2S_CLIENT_REGISTER);

  struct timespec lastHeartbeatTs = {0, 0};

  int rc = 0;
  while (client->status != ANKI_CAMERA_STATUS_OFFLINE) {
    // Process events or timeout after HEARTBEAT_INTERVAL
    rc = event_loop(client);
    if (rc == -1) {
      break;
    }

    if (client->status == ANKI_CAMERA_STATUS_IDLE) {
      if (client->request_start) {
        client->status = ANKI_CAMERA_STATUS_STARTING;
        enqueue_message(client, ANKI_CAMERA_MSG_C2S_START);
        client->request_start = 0;
      }
    }

    // send message to keep server alive
    struct timespec now = {0, 0};
    clock_gettime(CLOCK_MONOTONIC, &now);
    uint64_t elapsedNs = (now.tv_nsec + now.tv_sec * NSEC_PER_SEC) -
                         (lastHeartbeatTs.tv_nsec + lastHeartbeatTs.tv_sec * NSEC_PER_SEC);
    if (elapsedNs > HEARTBEAT_INTERVAL_NS) {
      enqueue_message(client, ANKI_CAMERA_MSG_C2S_HEARTBEAT);
      lastHeartbeatTs = now;
    }
  }

  // close socket
  if (client->fd >= 0) {
    close(client->fd);
  }

  // unmap & free ion mem
  rc = unmap_camera_capture_buf(client);
  if (rc != 0) {
    loge("%s: error unmapping capture buffer", __func__);
  }

  client->status = ANKI_CAMERA_STATUS_OFFLINE;

  return NULL;
}

//
// Public API
//

static struct anki_camera_handle_private s_camera_handle;

#define CAMERA_HANDLE_P(camera) ((struct anki_camera_handle_private *)(camera))

// Initializes the camera
int camera_init(struct anki_camera_handle **camera)
{
  // configure logging
  setAndroidLoggingTag("anki-cam-client");

  memset(&s_camera_handle, 0, sizeof(s_camera_handle));

  struct client_ctx *client = &s_camera_handle.camera_client;
  client->fd = 1;
  client->camera_buf.camera_capture_fd = -1;
  client->camera_buf.ion_fd = -1;

  int rc = socket_connect(&(client->fd));
  if (rc != 0) {
    loge("%s: connection error: %s", __func__, strerror(errno));
    return -1;
  }

  if (pthread_create(&client->ipc_thread, NULL, camera_client_thread, &s_camera_handle)) {
    loge("%s: error creating thread: %s", __func__, strerror(errno));
    return -1;
  }

  pthread_setname_np(client->ipc_thread, "EngCameraClient");

  client->is_running = 1;
  s_camera_handle.current_frame_id = UINT32_MAX;
  *camera = (struct anki_camera_handle *)&s_camera_handle;

  return 0;
}

// Starts capturing frames in new thread, sends them to callback `cb`.
int camera_start(struct anki_camera_handle *camera)
{
  // Start
  // received ack, start camera
  struct client_ctx *client = &CAMERA_HANDLE_P(camera)->camera_client;
  client->request_start = 1;
  return 0;
}

// Stops capturing frames
int camera_stop(struct anki_camera_handle *camera)
{
  // Stop
  enqueue_message(&CAMERA_HANDLE_P(camera)->camera_client, ANKI_CAMERA_MSG_C2S_STOP);
  return 0;
}

// De-initializes camera, makes it available to rest of system
int camera_release(struct anki_camera_handle *camera)
{
  enqueue_message(&CAMERA_HANDLE_P(camera)->camera_client, ANKI_CAMERA_MSG_C2S_CLIENT_UNREGISTER);

  if (pthread_join(CAMERA_HANDLE_P(camera)->camera_client.ipc_thread, NULL)) {
    loge("%s: error joining thread: %s", __func__, strerror(errno));
    return -1;
  }

  return 0;
}

// Attempt (lock) the last available frame for reading
int camera_frame_acquire(struct anki_camera_handle *camera, anki_camera_frame_t **out_frame)
{
  assert(camera != NULL);

  int rc = 0;
  struct client_ctx *client = &CAMERA_HANDLE_P(camera)->camera_client;
  uint8_t *data = client->camera_buf.data;
  if (data == NULL) {
    return -1;
  }
  anki_camera_buf_header_t *header = (anki_camera_buf_header_t *)data;

  // Get the most recent frame slot
  uint32_t slot = atomic_load(&header->locks.write_idx);

  // lock slot for reading
  uint32_t lock_status = 0;
  _Atomic uint32_t *slot_lock = &(header->locks.frame_locks[slot]);
  if (!atomic_compare_exchange_strong(slot_lock, &lock_status, 1)) {
    loge("%s: could not lock frame (slot: %u): %s", __func__, slot, strerror(errno));
    return -1;
  }

  const uint32_t frame_offset = header->frame_offsets[slot];
  anki_camera_frame_t *frame = (anki_camera_frame_t *)&data[frame_offset];

  if (frame->frame_id == CAMERA_HANDLE_P(camera)->current_frame_id) {
    logw("%s: duplicate frame: %u\n", __func__, frame->frame_id);
    rc = -1;
    goto UNLOCK;
  }

  CAMERA_HANDLE_P(camera)->current_frame_id = frame->frame_id;

  add_locked_slot(client, slot, frame->frame_id);
  if (out_frame != NULL) {
    *out_frame = frame;
  }

  return rc;

UNLOCK:

  // unlock slot
  lock_status = 1;
  if (!atomic_compare_exchange_strong(slot_lock, &lock_status, 0)) {
    loge("%s: could not unlock frame: %s", __func__, strerror(errno));
    rc = -1;
  }

  return rc;
}

// Release (unlock) frame to camera system
int camera_frame_release(struct anki_camera_handle *camera, uint32_t frame_id)
{
  int rc = 0;
  struct client_ctx *client = &CAMERA_HANDLE_P(camera)->camera_client;
  uint8_t *data = client->camera_buf.data;
  if (data == NULL) {
    return -1;
  }
  anki_camera_buf_header_t *header = (anki_camera_buf_header_t *)data;

  // Lookup slot;
  uint32_t slot;
  rc = get_locked_slot(client, frame_id, &slot);
  if (rc == -1) {
    loge("%s: failed to find slot for frame_id %u", __func__, frame_id);
    return rc;
  }

  // unlock slot
  uint32_t lock_status = 1;
  _Atomic uint32_t *slot_lock = &(header->locks.frame_locks[slot]);
  if (!atomic_compare_exchange_strong(slot_lock, &lock_status, 0)) {
    loge("%s: could not unlock frame (slot: %u): %s", __func__, slot, strerror(errno));
    rc = -1;
  }

  rc = remove_locked_slot(client, frame_id, NULL);

  return rc;
}

anki_camera_status_t camera_status(struct anki_camera_handle *camera)
{
  struct client_ctx *client = &CAMERA_HANDLE_P(camera)->camera_client;
  if (client == NULL) {
    return ANKI_CAMERA_STATUS_OFFLINE;
  }
  else {
    return client->status;
  }
}
