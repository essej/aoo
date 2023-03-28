# AOO v2.0 - message protocol

<br>

# 1 AOO source-sink communication

<br>

## `/aoo/sink/<id>/start`

Start a new stream.

### Arguments:

| type      | description                             |
| --------- | --------------------------------------- |
| `int32`   | source ID                               |
| `int32`   | version                                 |
| `int32`   | stream ID                               |
| `int32`   | flags                                   |
| `int32`   | format ID                               |
| `int32`   | number of channels                      |
| `int32`   | sample rate                             |
| `int32`   | block size                              |
| `str`     | codec name                              |
| `blob`    | codec extension                         |
| [`int32`] | (opt.) [metadata type](#5.1-Data-Types) |
| [`blob`]  | (opt.) metadata content                 |

---

## `/aoo/source/<id>/start`

Request a start message.

### Arguments:

| type    | description |
| ------- | ----------- |
| `int32` | sink ID     |
| `int32` | version     |

---

## `/aoo/sink/<id>/stop`

Stop a stream.

### Arguments:

| type    | description |
| ------- | ----------- |
| `int32` | source ID   |
| `int32` | stream ID   |

---

## `/aoo/sink/<id>/data`

Send stream data.

### Arguments:

| type     | description            |
| -------- | ---------------------- |
| `int32`  | source ID              |
| `int32`  | stream ID              |
| `int32`  | sequence number        |
| `double` | real sample rate       |
| `int32`  | channel onset          |
| `int32`  | total data size        |
| `int32`  | message data size      |
| `int32`  | total number of frames |
| `int32`  | frame index            |
| `blob`   | data content           |

---

## `/aoo/source/<id>/data`

Request stream data.

### Arguments:

| type      | description     |
| --------- | --------------- |
| `int32`   | source ID       |
| `int32`   | stream ID       |
| `int32`   | sequence 1      |
| `int32`   | frame index 1   |
| [`int32`] | [sequence 2]    |
| [`int32`] | [frame index 2] |
| ...       | ...             |

---

## `/aoo/source/<id>/invite`

Invite a source.

### Arguments:

| type      | description                             |
| --------- | --------------------------------------- |
| `int32`   | sink ID                                 |
| `int32`   | stream ID                               |
| [`int32`] | (opt.) [metadata type](#5.1-Data-Types) |
| [`blob`]  | (opt.) metadata content                 |

---

## `/aoo/source/<id>/uninvite`

Uninvite a source.

### Arguments:

| type    | description |
| ------- | ----------- |
| `int32` | sink ID     |
| `int32` | stream ID   |

---

## `/aoo/sink/<id>/decline`

Decline an invitation.

### Arguments:

| type    | description |
| ------- | ----------- |
| `int32` | source ID   |
| `int32` | stream ID   |

---

## `/aoo/[source|sink]/<id>/ping`

Send a ping.

### Arguments:

| type    | description    |
| ------- | -------------- |
| `int32` | sink/source ID |
| `int32` | token          |
| `tt`    | timestamp      |

---

## `/aoo/[source|sink]/<id>/pong`

Reply to ping message.

### Arguments:

| type      | description                   |
| --------- | ----------------------------- |
| `int32`   | sink/source ID                |
| `tt`      | timestamp 1 (remote)          |
| `tt`      | timestamp 2 (local)           |
| [`float`] | (opt.) packet loss percentage |

<br>

# 2 AOO peer communication

<br>

## `/aoo/peer/ping`

Send a ping to a peer.

### Arguments:

| type    | description      |
| ------- | ---------------- |
| `int32` | group ID         |
| `int32` | peer ID (sender) |
| `tt`    | timestamp        |

Note: *timestamp* is empty (0) in handshake pings.

---

## `/aoo/peer/pong`

Reply to ping message.

### Arguments:

| type      | description          |
| --------- | -------------------- |
| `int32`   | group ID             |
| `int32`   | peer ID (sender)     |
| `tt`      | timestamp 1 (remote) |
| `tt`      | timestamp 2 (local)  |

Note: Both *timestamps* are empty (0) in handshake pongs.

---

## `/aoo/peer/msg`

Send a message to a peer.

### Arguments:

| type    | description                      |
| ------- | -------------------------------- |
| `int32` | group ID                         |
| `int32` | peer ID (sender)                 |
| `int32` | flags                            |
| `int32` | sequence number                  |
| `int32` | total message size               |
| `int32` | number of frames                 |
| `int32` | frame index                      |
| `tt`    | timetag                          |
| `int32` | [message type](#5.1-Data-Types)  |
| `blob`  | message content                  |

Possible values for *flags*:

- `0x01`: reliable transmission

---

## `/aoo/peer/ack`

Send message acknowledgements.

### Arguments:

| type      | description      |
| --------- | ---------------- |
| `int32`   | group ID         |
| `int32`   | peer ID (sender) |
| `int32`   | count            |
| `int32`   | sequence 1       |
| `int32`   | frame 1          |
| [`int32`] | [sequence 2]     |
| [`int32`] | [frame 2]        |
| ...       | ...              |

<br>

## 3 AOO client-server communication

<br>

## 3.1 Client requests and server responses

<br>

## `/aoo/server/query`

Sent query to server.

### Arguments:

None

---

## `/aoo/client/query`

Reply to query.

### Arguments:

| type    | description     |
| ------- | --------------- |
| `str`   | public IP       |
| `int32` | public port     |
| `str`   | TCP server IP   |
| `int32` | TCP server port |

---

## `/aoo/server/login`

Login to server.

### Arguments:

| type    | description                            |
| ------- | -------------------------------------- |
| `int32` | request token                          |
| `int32` | version                                |
| `str`   | password (encrypted)                   |
| `int32` | [metadata type](#5.1-Data-Types)       |
| `blob`  | (opt.) metadata content (may be empty) |
| `int32`   | public address count                 |
| `str`     | public IP 1                          |
| `int32`   | public port 1                        |
| [`str`]   | [public IP 2]                        |
| [`int32`] | [public port 2]                      |
| ...       | ...                                  |

---

## `/aoo/client/login`

Login response.

### Arguments:

| type      | description                             |
| --------- | --------------------------------------- |
| `int32`   | request token                           |
| `int32`   | success (1) or failure (0)              |
| success   |                                         |
| `int32`   | client ID                               |
| `int32`   | flags                                   |
| [`int32`] | (opt.) [metadata type](#5.1-Data-Types) |
| [`blob`]  | (opt.) metadata content                 |
| failure   |                                         |
| `int32`   | error code                              |
| `str`     | error message                           |

Possible values for *flags*:

- 0x01: server relay supported

---

## `/aoo/server/group/join`

Join a group on the server.

### Arguments:

| type      | description                            |
| --------- | -------------------------------------- |
| `int32`   | request token                          |
| `str`     | group name                             |
| `str`     | group password (encrypted)             |
| `int32`   | group [metadata type](#5.1-Data-Types) |
| `blob`    | group metadata content                 |
| `str`     | user name                              |
| `str`     | user password (encrypted)              |
| `int32`   | user [metadata type](#5.1-Data-Types)  |
| `blob`    | user metadata content                  |
| [`str`]   | (opt.) relay IP                        |
| [`int32`] | (opt.) relay port                      |

---

## `/aoo/client/group/join`

Group join response.

### Arguments:

| type      | description                              |
| --------- | ---------------------------------------- |
| `int32`   | request token                            |
| `int32`   | success (1) or failure (0)               |
| success   |                                          |
| `int32`   | group ID                                 |
| `int32`   | user ID                                  |
| [`int32`] | group [metadata type](#5.1-Data-Types)   |
| [`blob`]  | group metadata content                   |
| [`int32`] | user [metadata type](#5.1-Data-Types)    |
| [`blob`]  | user metadata content                    |
| [`int32`] | private [metadata type](#5.1-Data-Types) |
| [`blob`]  | private metadata content                 |
| failure |                                            |
| `int32` | error code                                 |
| `str`   | error message                              |

---

## `/aoo/server/group/leave`

Leave a group on the server.

### Arguments:

| type    | description   |
| ------- | ------------- |
| `int32` | request token |
| `int32` | group ID      |

---

## `/aoo/client/group/leave`

Group leave response.

### Arguments:

| type    | description                |
| ------- | -------------------------- |
| `int32` | request token              |
| `int32` | success (1) or failure (0) |
| failure |                            |
| `int32` | error code                 |
| `str`   | error message              |

---

## `/aoo/server/group/update`

Update group metadata.

### Arguments:

| type    | description                      |
| ------- | -------------------------------- |
| `int32` | request token                    |
| `int32` | group ID                         |
| `int32` | [metadata type](#5.1-Data-Types) |
| `blob`  | metadata content                 |

---

## `/aoo/client/group/update`

Group update response.

### Arguments:

| type    | description                      |
| ------- | -------------------------------- |
| `int32` | request token                    |
| `int32` | success (1) or failure (0)       |
| success |                                  |
| `int32` | [metadata type](#5.1-Data-Types) |
| `blob`  | metadata content                 |
| failure |                                  |
| `int32` | error code                       |
| `str`   | error message                    |

---

## `/aoo/server/user/update`

Update user metadata.

### Arguments:

| type    | description                      |
| ------- | -------------------------------- |
| `int32` | request token                    |
| `int32` | group ID                         |
| `int32` | user ID                          |
| `int32` | [metadata type](#5.1-Data-Types) |
| `blob`  | metadata content                 |

---

## `/aoo/client/user/update`

User update response.

### Arguments:

| type    | description                      |
| ------- | -------------------------------- |
| `int32` | request token                    |
| `int32` | success (1) or failure (0)       |
| success |                                  |
| `int32` | [metadata type](#5.1-Data-Types) |
| `blob`  | metadata content                 |
| failure |                                  |
| `int32` | error code                       |
| `str`   | error message                    |

---

## `/aoo/server/request`

Send custom request.

### Arguments:

| type    | description                  |
| ------- | ---------------------------- |
| `int32` | request token                |
| `int32` | flags                        |
| `int32` | [data type](#5.1-Data-Types) |
| `blob`  | data content                 |

---

## `/aoo/client/request`

Response to custom request.

### Arguments:

| type    | description                  |
| ------- | ---------------------------- |
| `int32` | request token                |
| `int32` | success (1) or failure (0)   |
| success |                              |
| `int32` | flags                        |
| `int32` | [data type](#5.1-Data-Types) |
| `blob`  | data content                 |
| failure |                              |
| `int32` | error code                   |
| `str`   | error message                |

<br>

## 3.2 Server notifications

<br>

## `/aoo/client/peer/join`

A peer has joined the group.

### Arguments:

| type      | description                  |
| --------- | -----------------------------|
| `str`     | group name                   |
| `int32`   | group ID                     |
| `str`     | peer name                    |
| `int32`   | peer ID                      |
| `blob`    | peer metadata (may be empty) |
| `int32`   | public address count         |
| `str`     | public IP 1                  |
| `int32`   | public port 1                |
| [`str`]   | [public IP 2]                |
| [`int32`] | [public port 2]              |
| ...       | ...                          |
| [`str`]   | (opt.) relay IP              |
| [`int32`] | (opt.) relay port            |

---

## `/aoo/client/peer/leave`

A peer has left the group.

### Arguments:

| type    | description      |
| ------- | ---------------- |
| `int32` | group ID         |
| `int32` | peer ID          |

---

## `/aoo/client/peer/changed`

Peer metadata has changed.

### Arguments:

| type    | description                      |
| ------- | -------------------------------- |
| `int32` | group ID                         |
| `int32` | peer ID                          |
| `int32` | [metadata type](#5.1-Data-Types) |
| `blob`  | metadata content                 |

---

## `/aoo/client/user/changed`

User metadata has changed.

### Arguments:

| type    | description                      |
| ------- | -------------------------------- |
| `int32` | group ID                         |
| `int32` | user ID                          |
| `int32` | [metadata type](#5.1-Data-Types) |
| `blob`  | metadata content                 |

---

## `/aoo/client/group/changed`

Group metadata has changed.

### Arguments:

| type    | description                      |
| ------- | -------------------------------- |
| `int32` | group ID                         |
| `int32` | [metadata type](#5.1-Data-Types) |
| `blob`  | metadata content                 |

---

## `/aoo/client/message`

Generic server notification.

### Arguments:

| type    | description                      |
| ------- | -------------------------------- |
| `int32` | [metadata type](#5.1-Data-Types) |
| `blob`  | message content                  |

<br>

## 3.3 Other

<br>

## `/aoo/server/ping`

Send a ping message to the server.

### Arguments:

None

---

## `/aoo/client/pong`

Reply with pong message to the client.

### Arguments:

None

<br>

# 4 Relay

<br>

## `/aoo/relay`

Relayed message.

### Arguments:

| type    | description      |
| ------- | ---------------- |
| `str`   | destination IP   |
| `int32` | destination port |
| `blob`  | message data     |

<br>

# 5 Constants

<br>

### 5.1 Codec names

| value  | description |
| ------ | ----------- |
| "pcm"  | PCM codec   |
| "opus" | Opus codec  |

<br>

### 5.1 Data types

| value | description                |
| ----- | -------------------------- |
| -1    | Unspecified                |
| 0     | Raw/binary data            |
| 1     | plain text (UTF-8)         |
| 2     | OSC (Open Sound Control)   |
| 3     | MIDI                       |
| 4     | FUDI (Pure Data)           |
| 5     | JSON (UTF-8)               |
| 6     | XML (UTF-8)                |
| 1000< | User specified             |

<br>

# 6 Binary messages

<br>

TODO
