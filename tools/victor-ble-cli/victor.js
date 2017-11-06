const { StringDecoder } = require('string_decoder');

const WiFiAuth = {
    AUTH_NONE_OPEN : {value: 0, name: "None" },
    AUTH_NONE_WEP :  {value: 1, name: "WEP" },
    AUTH_NONE_WEP_SHARED : {value: 2, name: "WEP Shared"},
    AUTH_IEEE8021X: {value: 3, name: "IEEE8021X"},
    AUTH_WPA_PSK: {value: 4, name: "WPA PSK"},
    AUTH_WPA_EAP: {value: 5, name: "WPA EAP"},
    AUTH_WPA2_PSK: {value: 6, name: "WPA2 PSK"},
    AUTH_WPA2_EAP: {value: 7, name: "WPA2 EAP"}
};

class Victor {
    constructor(peripheral, service, send, read, outputCallback) {

        this._peripheral = peripheral;
        this._service = service;
        this._send_char = send;
        this._read_char = read;
        this._output = outputCallback;
        this._outgoing_packets = [];
        this._incoming_packets = [];
        this._heartbeat_counter = 0;

        this._peripheral.on('disconnect', () => {
            clearInterval(this._interval);
        });

        this._interval = setInterval(() => {
            if (this._outgoing_packets.length == 0) { return ;}

            var packet = this._outgoing_packets.shift();

            this._send_char.write(packet);
        }, 10);

        var handleMessage = function(data) {
            if (!data) {
                return;
            }
            if (data.length < 2) {
                return;
            }
            var size = data[0];
            var msgID = data[1];
            switch (msgID) {
            case Victor.MSG_V2B_CORE_PING_RESPONSE:
                this._output("Ping Response");
                return;
            case Victor.MSG_V2B_HEARTBEAT:
                this._heartbeat_counter = data[2];
                this._output("Heartbeat " + this._heartbeat_counter);
                return;
            case Victor.MSG_V2B_WIFI_SCAN_RESULTS:
                var offset = 0;
                var buf = data.slice(2);
                var results = "";
                while (offset < buf.length) {
                    var auth = buf.readUInt8(offset++);
                    var encrypted = buf.readUInt8(offset++);
                    var wps = buf.readUInt8(offset++);
                    var signal_level = buf.readUInt8(offset++);
                    var end = buf.indexOf(0, offset);
                    if (end < offset) {
                        return;
                    }
                    const decoder = new StringDecoder('utf8');
                    var ssid = decoder.write(buf.slice(offset, end));
                    ssid += decoder.end();
                    offset = end + 1;
                    switch (auth) {
                    case WiFiAuth.AUTH_NONE_OPEN.value:
                        results += WiFiAuth.AUTH_NONE_OPEN.name;
                        break;
                    case WiFiAuth.AUTH_NONE_WEP.value:
                        results += WiFiAuth.AUTH_NONE_WEP.name;
                        break;
                    case WiFiAuth.AUTH_NONE_WEP_SHARED.value:
                        results += WiFiAuth.AUTH_NONE_WEP_SHARED.name;
                        break;
                    case WiFiAuth.AUTH_IEEE8021X.value:
                        results += WiFiAuth.AUTH_IEEE8021X.name;
                        break;
                    case WiFiAuth.AUTH_WPA_PSK.value:
                        results += WiFiAuth.AUTH_WPA_PSK.name;
                        break;
                    case WiFiAuth.AUTH_WPA_EAP.value:
                        results += WiFiAuth.AUTH_WPA_EAP.name;
                        break;
                    case WiFiAuth.AUTH_WPA2_PSK.value:
                        results += WiFiAuth.AUTH_WPA2_PSK.name;
                        break;
                    case WiFiAuth.AUTH_WPA2_PSK.value:
                        results += WiFiAuth.AUTH_WPA2_PSK.name;
                        break;
                    default:
                        result += "Unknown (" + auth + ")";
                        break;
                    };
                    results += "\t";
                    if (encrypted) {
                        results += "Encrypted";
                    } else {
                        results += "Not Encrypted";
                    }
                    results += "\t";
                    if (wps) {
                        results += "WPS";
                    } else {
                        results += "   ";
                    }
                    results += "\t";
                    for (var i = 0 ; i < signal_level; i++) {
                        results += "*";
                    }
                    results += "\t" + ssid + "\n";
                }
                this._output(results);
                return;
            case Victor.MSG_V2B_DEV_EXEC_CMD_LINE_RESPONSE:
                this._output(data.toString('utf8', 2, data.length));
                return;
            case Victor.MSG_V2B_MULTIPART_START:
                this._incoming_packets = [];
                this._incoming_packets.push(Buffer.from(data));
                return;
            case Victor.MSG_V2B_MULTIPART_CONTINUE:
                this._incoming_packets.push(Buffer.from(data));
                return;
            case Victor.MSG_V2B_MULTIPART_FINAL:
                this._incoming_packets.push(Buffer.from(data));
                var totalLength = 0;
                for (var i = 0 ; i < this._incoming_packets.length ; i++) {
                    totalLength += (this._incoming_packets[i].length - 2);
                }
                var buf = Buffer.alloc(totalLength);
                var targetStart = 0;
                for (var i = 0 ; i < this._incoming_packets.length ; i++) {
                    targetStart += this._incoming_packets[i].copy(buf, targetStart, 2);
                }
                handleMessage.bind(this)(buf);
                this._incoming_packets = [];
                return;
            default:
                return;
            };

        };
        read.on('data', handleMessage.bind(this));
        read.subscribe();
    };

    _send (buffer) {
        this._outgoing_packets.push(buffer);
    };

    send (msgID, body) {
        var size = Victor.MSG_BASE_SIZE;
        if (body) {
            size += body.length;
        }
        var buf = Buffer.from([size, msgID]);
        if (body) {
            buf = Buffer.concat([buf, body]);
        }
        if (buf.length > Victor.MSG_MAX_SIZE) {
            var off = 0;
            while (off < buf.length) {
                var msgSize = Victor.MSG_BASE_SIZE;
                var id;
                if (off == 0) {
                    id = Victor.MSG_B2V_MULTIPART_START;
                    msgSize += Victor.MSG_PAYLOAD_MAX_SIZE;
                } else if ((buf.length - off) > Victor.MSG_PAYLOAD_MAX_SIZE) {
                    id = Victor.MSG_B2V_MULTIPART_CONTINUE;
                    msgSize += Victor.MSG_PAYLOAD_MAX_SIZE;
                } else {
                    id = Victor.MSG_B2V_MULTIPART_FINAL;
                    msgSize += (buf.length - off);
                }
                const mhdr = Buffer.from([msgSize, id]);
                const mbuf = Buffer.concat([mhdr, buf.slice(off, off + msgSize - 1)]);
                this._send(mbuf);
                off += (msgSize - Victor.MSG_BASE_SIZE);
            }
        } else {
            this._send(buf);
        }
    };


    disconnect () {
        this._peripheral.disconnect();
    };
}

Object.defineProperty(Victor, 'MSG_MAX_SIZE', {value: 20, writable: false});
Object.defineProperty(Victor, 'MSG_PAYLOAD_MAX_SIZE', {value: 18, writable: false});
Object.defineProperty(Victor, 'MSG_BASE_SIZE', {value: 1, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_BTLE_DISCONNECT', {value: 0x0D, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_CORE_PING_REQUEST', {value: 0x16, writable: false});
Object.defineProperty(Victor, 'MSG_V2B_CORE_PING_RESPONSE', {value: 0x17, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_HEARTBEAT', {value: 0x18, writable: false});
Object.defineProperty(Victor, 'MSG_V2B_HEARTBEAT', {value: 0x19, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_WIFI_START', {value: 0x1A, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_WIFI_STOP', {value: 0x1B, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_WIFI_SET_CONFIG', {value: 0x1C, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_WIFI_SCAN', {value: 0x1D, writable: false});
Object.defineProperty(Victor, 'MSG_V2B_WIFI_SCAN_RESULTS', {value: 0x1E, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_SSH_SET_AUTHORIZED_KEYS', {value: 0x80, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_DEV_PING_WITH_DATA_REQUEST', {value: 0x91, writable: false});
Object.defineProperty(Victor, 'MSG_V2B_DEV_PING_WITH_DATA_RESPONSE', {value: 0x92, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_DEV_RESTART_ADBD', {value: 0x93, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_DEV_EXEC_CMD_LINE', {value: 0x94, writable: false});
Object.defineProperty(Victor, 'MSG_V2B_DEV_EXEC_CMD_LINE_RESPONSE', {value: 0x95, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_MULTIPART_START', {value: 0xF0, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_MULTIPART_CONTINUE', {value: 0xF1, writable: false});
Object.defineProperty(Victor, 'MSG_B2V_MULTIPART_FINAL', {value: 0xF2, writable: false});
Object.defineProperty(Victor, 'MSG_V2B_MULTIPART_START', {value: 0xF3, writable: false});
Object.defineProperty(Victor, 'MSG_V2B_MULTIPART_CONTINUE', {value: 0xF4, writable: false});
Object.defineProperty(Victor, 'MSG_V2B_MULTIPART_FINAL', {value: 0xF5, writable: false});

module.exports = Victor;
