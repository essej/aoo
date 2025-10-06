AooClient {
	classvar <>clients;
	classvar nextToken = 0;

	var <>server;
	var <>port;
	var <>dispatcher;
	var <>peers;
	var <>groups;
	var <>state;

	var eventOSCFunc;
	var msgOSCFunc;
	var eventFuncs;
	var replyAddr;
	var nodeAddr;

	*initClass {
		var doFree = {
			clients.values.do(_.free);
			clients.clear;
		};
		clients = IdentityDictionary.new;
		ServerQuit.add(doFree);
		CmdPeriod.add(doFree);
	}

	*find { arg port;
		^clients[port];
	}

	*new { arg port, server;
		^super.new.init(port, server);
	}

	init { arg port, server;
		var localAddr = NetAddr.localAddr;

		if (clients[port].notNil) {
			^Error("AooClient on port % already exists!".format(port)).throw;
		};

		this.server = server ?? Server.default;
		this.peers = [];
		this.state = \disconnected;
		this.dispatcher = AooDispatcher(this);
		eventFuncs = IdentityDictionary();
		replyAddr = NetAddr(this.server.addr.ip, port);

		Aoo.prGetReplyAddr(port, this.server, { |addr|
			if (addr.notNil) {
				// create AooClient on the server
				OSCFunc({ arg msg;
					var success = msg[2].asBoolean;
					success.if {
						this.prPostInit(port, addr);
					} {
						"Couldn't create AooClient on port %: %".format(port, msg[3]).error;
					};
				}, '/aoo/client/new', argTemplate: [port]).oneShot;

				this.server.sendMsg('/cmd', '/aoo_client_new', port);
			}
		});
	}

	isOpen { ^this.port.notNil }

	prPostInit { arg port, addr;
		this.port = port;
		replyAddr = addr;
		nodeAddr = NetAddr(addr.ip, port);

		// handle events
		eventOSCFunc = OSCFunc({ arg msg;
			this.prHandleEvent(msg[2], msg[3..]);
		}, '/aoo/client/event', addr, argTemplate: [port]);

		// handle messages
		msgOSCFunc = OSCFunc({ arg msg, time;
			this.prHandleMsg(time, *msg[2..]);
		}, '/aoo/client/msg', addr, argTemplate: [port]);

		ServerQuit.add { this.free };

		clients[port] = this;
	}

	free {
		eventOSCFunc.free;
		msgOSCFunc.free;
		replyAddr = nil;
		nodeAddr = nil;
		port.notNil.if {
			server.sendMsg('/cmd', '/aoo_client_free', port);
			if (clients[port] === this) {
				clients[port] = nil;
			} { "bug: AooClient not registered".error }
		};
		server = nil;
		port = nil;
		peers = nil;
		groups = nil;
	}

	addListener { arg type, func;
		eventFuncs[type] = eventFuncs[type].addFunc(func);
	}

	removeListener { arg type, func;
		if (func.notNil) {
			eventFuncs[type] = eventFuncs[type].removeFunc(func);
		} {
			eventFuncs.removeAt(type);
		}
	}

	prHandleEvent { arg type, args;
		// \disconnect, \peerJoin, \peerLeave, \peerHandshake, \peerTimeout, \peerPing
		var md, peer, group;
		var event = type.switch(
			\disconnect, {
				"disconnected from server".error;
				AooError(args[0], args[1])
			},
			\peerJoin, {
				md = AooData.fromBytes(*args[6..7]);
				peer = AooPeer.prFromEvent(*(args[0..5] ++ md));
				this.prAddPeer(peer);
				peer
			},
			\peerLeave, {
				peer = AooPeer.prFromEvent(*args[0..5]);
				this.prRemovePeer(peer);
				peer
			},
			\peerHandshake, {
				AooPeer.prFromEvent(*args[0..5])
			},
			\peerTimeout, {
				AooPeer.prFromEvent(*args[0..5])
			},
			\peerPing, {
				peer = AooPeer.prFromEvent(*args[0..1]);
				peer = this.prFindPeer(peer, true);
				if (peer.notNil) { [peer] ++ args[2..] } { nil }
			},
			\peerUpdate, {
				md = AooData.fromBytes(*args[2..3]);
				peer = AooPeer.prFromEvent(*args[0..1]);
				peer = this.prFindPeer(peer, true);
				if (peer.notNil) { peer.user.metadata = md; [peer] } { nil }
			},
			\groupUpdate, {
				md = AooData.fromBytes(*args[1..2]);
				group = AooGroup(nil, args[0]);
				group = this.prFindGroup(group, true);
				if (group.notNil) { group.metadata = md; [group] } { nil }
			},
			{ "%: ignore unknown event '%'".format(this.class.name, type).warn; nil }
		);
		if (event.notNil) {
			eventFuncs[type].value(*event);
			eventFuncs[\event].value(type, event);
		}
	}

	prHandleMsg { arg time, group, user, type, data;
		var msg, peer, func = eventFuncs[\msg];
		if (func.notNil) {
			peer = AooPeer.prFromEvent(group, user);
			peer = this.prFindPeer(peer, true);
			if (peer.notNil) {
				msg = AooData.fromBytes(type, data);
				if (msg.notNil) {
					func.value(msg, time, peer);
				}
			}
		}
	}

	prAddPeer { arg peer;
		var group;
		if (this.prFindPeer(peer).isNil) {
			group = this.prFindGroup(peer.group);
			if (group.notNil) {
				peer.group = group;
				this.peers = this.peers.add(peer);
			} {
				"cannot add peer %: group not found".format(peer).error;
			}
		} {
			"peer % already added".format(peer).error;
		}
	}

	prRemovePeer { arg peer;
		var index = this.peers.indexOfEqual(peer);
		if (index.notNil) { this.peers.removeAt(index) }
		{ "could not remove peer %".format(peer).error }
	}

	prFindPeer { arg peer, loud=false;
		peers.do { |p|
			if (p == peer) { ^p }
		};
		if (loud) { "could not find peer %".format(peer).error };
		^nil;
	}

	prAddGroup { arg group;
		if (this.prFindGroup(group).isNil) {
			this.groups = this.groups.add(group)
		} {
			"group % already added".format(group).error;
		}
	}

	prRemoveGroup { arg group;
		var index;
		// remove all peers that belong to this group!
		this.peers = this.peers.select { |p| p.group != group };
		// remove the group itself
	    index = this.groups.indexOfEqual(group);
		if (index.notNil) { this.groups.removeAt(index) }
		{ "could not remove group %".format(group).error }
	}

	prFindGroup { arg group, loud = false;
		groups.do { |g|
			if (g == group) { ^g }
		};
		if (loud) { "could not find group %".format(group).error };
		^nil;
	}

	connect { arg hostname, port, password, metadata, timeout=5, action;
		var resp, token, watchdog;
		this.port ?? { MethodError("AooClient: not initialized", this).throw };

		state.switch(
			\connected, { ^MethodError("already connected", this).throw },
			\connecting, { ^MethodError("still connecting", this).throw }
		);
		state = \connecting;

		port = port ?? AooServer.defaultPort;
		password = password ?? "";
		token = this.class.prNextToken;

		resp = OSCFunc({ arg msg;
			var err, clientID, version, metadata;
			watchdog.stop; // cancel watchdog Routine!
			if (msg[3] == 0) {
				clientID = msg[4];
				version = msg[5];
				metadata = AooData.fromBytes(*msg[6..7]);
				// "%: connected to % % (client ID: %)".format(this.class.name, hostname, port, clientID).postln;
				state = \connected;
				action.value(nil, clientID, version, metadata);
			} {
				err = AooError(*msg[3..4]);
				"AooClient: could not connect to % %: %".format(hostname, port, err.message).error;
				state = \disconnected;
				action.value(err);
			};
		}, '/aoo/client/connect', replyAddr, argTemplate: [this.port, token]).oneShot;

		// We need a timeout mechanism in case the reply message gets lost
		// and leaves the AooClient in limbo...
		// NOTE: the timeout must be larger than the connection timeout!
		watchdog = fork {
			(timeout * 2).wait;
			(state == \connecting).if {
				"AooClient: connect method timed out with no reply".error;
				resp.free;
				state = \disconnected;
				action.value(false, "time out");
			}
		};

		if (metadata.notNil) {
			server.sendMsg('/cmd', '/aoo_client_connect',
				this.port, token, hostname, port, password, *metadata.asOSCArgArray);
		} {
			server.sendMsg('/cmd', '/aoo_client_connect',
				this.port, token, hostname, port, password, $N, $N);
		}
	}

	disconnect { arg action;
		var token;
		this.port ?? { MethodError("AooClient not initialized", this).throw };
		(state != \connected).if {
			"AooClient: not connected".warn;
			^this;
		};
		token = this.class.prNextToken;

		OSCFunc({ arg msg;
			var err;
			if (msg[3] == 0) {
				// remove all groups and peers
				this.peers = [];
				this.groups = [];
				// "AooClient: disconnected".postln;
			} {
				err = AooError(*msg[3..4]);
				"AooClient: could not disconnect: %".format(err.message).error;
			};
			state = \disconnected;
			action.value(err);
		}, '/aoo/client/disconnect', replyAddr, argTemplate: [this.port, token]).oneShot;

		server.sendMsg('/cmd', '/aoo_client_disconnect', this.port, token);
	}

	joinGroup { arg groupName, userName, groupPassword, userPassword, groupMetadata, userMetadata, relayAddr, action;
		var token, args;
		this.port ?? { ^MethodError("AooClient not initialized", this).throw };
		if (state != \connected) {
			^MethodError("not connected to an AOO server", this).throw
		};
		token = this.class.prNextToken;
		groupPassword = groupPassword ?? { "" };
		userPassword = userPassword ?? { "" };

		OSCFunc({ arg msg;
			var err, groupID, userID, user, group;
			var groupMetadata, userMetadata, privateMetadata, relayAddr;
			if (msg[3] == 0) {
				groupID = msg[4];
				userID = msg[5];
				groupMetadata = AooData.fromBytes(*msg[6..7]);
				userMetadata = AooData.fromBytes(*msg[8..9]);
				privateMetadata = AooData.fromBytes(*msg[10..11]);
				"AooClient: joined group '%' as user '%' (group ID: %, user ID: %)".format(groupName, userName, groupID, userID).postln;
				user = AooUser(userName, userID, userMetadata);
				group = AooGroup(groupName, groupID, groupMetadata).user_(user);
				this.prAddGroup(group);
				action.value(nil, group, user, privateMetadata);
			} {
				err = AooError(*msg[3..4]);
				"AooClient: could not join group '%': %".format(groupName, err.message).error;
				action.value(err);
			};
		}, '/aoo/client/group/join', replyAddr, argTemplate: [this.port, token]).oneShot;

		// port, token, group name, group pwd, [group metadata],
		// user name, user pwd, [user metadata], [relay address]
		args = [this.port, token, groupName, groupPassword, userName, userPassword];
		if (groupMetadata.notNil) {
			args = args ++ groupMetadata.asOSCArgArray;
		} { args.add($N).add($N) };
		if (userMetadata.notNil) {
			args = args ++ userMetadata.asOSCArgArray;
		} { args.add($N).add($N) };
		if (relayAddr.notNil) {
			args = args.add(relayAddr.ip).add(relayAddr.port);
		} { args.add($N).add($N) };

		server.sendMsg('/cmd', '/aoo_client_group_join', *args);
	}

	leaveGroup { arg group, action;
		var token, name;
		this.port ?? { MethodError("AooClient not initialized", this).throw };
		token = this.class.prNextToken;
		if (group.isNil) {
			// take the first (and only) group
			if (this.groups.size == 0) {
				^MethodError("not a group member", this).throw
			};
			if (this.groups.size > 1) {
				^MethodError("member of multiple groups", this).throw
			};
			group = this.groups[0];
		} {
			if (group.isKindOf(AooGroup).not) {
				group = AooGroup(group);
			};
			group = this.prFindGroup(group);
			if (group.isNil) { ^MethodError("not a group member", this).throw };
		};

		OSCFunc({ arg msg;
			var err;
			if (msg[3] == 0) {
				"AooClient: left group '%'".format(group.name).postln;
				this.prRemoveGroup(group);
			} {
				err = AooError(*msg[3..4]);
				"AooClient: could not leave group '%': %".format(group.name, err.message).error;
			};
			action.value(err);
		}, '/aoo/client/group/leave', replyAddr, argTemplate: [this.port, token]).oneShot;

		server.sendMsg('/cmd', '/aoo_client_group_leave', this.port, token, group.id);
	}

	updateGroup { arg group, groupMetadata, action;
		var token;
		this.port ?? { ^MethodError("AooClient not initialized", this).throw };
		if (state != \connected) {
			^MethodError("not connected to an AOO server", this).throw
		};
		token = this.class.prNextToken;
		if (group.isNil) {
			// take the first (and only) group
			if (this.groups.size == 0) {
				^MethodError("not a group member", this).throw
			};
			if (this.groups.size > 1) {
				^MethodError("member of multiple groups", this).throw
			};
			group = this.groups[0];
		} {
			if (group.isKindOf(AooGroup).not) {
				group = AooGroup(group);
			};
			group = this.prFindGroup(group);
			if (group.isNil) { ^MethodError("not a group member", this).throw };
		};

		OSCFunc({ arg msg;
			var err;
			if (msg[3] == 0) {
				group.metadata = AooData.fromBytes(*msg[4..5]);
				"AooClient: updated group '%'".format(group.name).postln;
				action.value(nil, group);
			} {
				err = AooError(*msg[3..4]);
				"AooClient: could not update group '%': %".format(group.name, err.message).error;
				action.value(err);
			};
		}, '/aoo/client/group/update', replyAddr, argTemplate: [this.port, token]).oneShot;

		server.sendMsg('/cmd', '/aoo_client_group_update', this.port, token,
			group.id, *groupMetadata.asOSCArgArray);
	}

	updateUser { arg group, userMetadata, action;
		var token;
		this.port ?? { ^MethodError("AooClient not initialized", this).throw };
		if (state != \connected) {
			^MethodError("not connected to an AOO server", this).throw
		};
		token = this.class.prNextToken;
		if (group.isNil) {
			// take the first (and only) group
			if (this.groups.size == 0) {
				^MethodError("not a group member", this).throw
			};
			if (this.groups.size > 1) {
				^MethodError("member of multiple groups", this).throw
			};
			group = this.groups[0];
		} {
			if (group.isKindOf(AooGroup).not) {
				group = AooGroup(group);
			};
			group = this.prFindGroup(group);
			if (group.isNil) { ^MethodError("not a group member", this).throw };
		};

		OSCFunc({ arg msg;
			var err;
			if (msg[3] == 0) {
				group.user.metadata = AooData.fromBytes(*msg[4..5]);
				"AooClient: updated user '%' in group '%'"
				.format(group.user.name, group.name).postln;
				action.value(nil, group, group.user);
			} {
				err = AooError(*msg[3..4]);
				"AooClient: could not update user '%' in group '%': %"
				.format(group.user.name, group.name, err.message).error;
				action.value(err);
			};
		}, '/aoo/client/user/update', replyAddr, argTemplate: [this.port, token]).oneShot;

		server.sendMsg('/cmd', '/aoo_client_user_update', this.port, token,
			group.id, *userMetadata.asOSCArgArray);
	}

	sendMsg { arg target, time, msg, reliable = false;
		var oscMsg, groupID = -1, userID = -1, group, peer;
		if (msg.isKindOf(Array)) {
			// interpret Array as OSC message
			msg = AooData(\osc, msg);
		};
		if (msg.class != AooData) {
			^MethodError("'msg' must be Array or AooData", this).throw;
		};
		if (target.notNil) {
			if (target.isKindOf(AooPeer)) {
				// peer
				peer = this.prFindPeer(target);
				if (peer.isNil) {
					^MethodError("could not find peer %".format(target), this).throw
				};
				groupID = peer.group.id;
				userID = peer.user.id;
			} {
				// group
				if (target.isKindOf(AooGroup)) {
					group = this.prFindGroup(target);
					if (group.isNil) {
						^MethodError("could not find group %".format(target), this).throw
					};
					groupID = group.id;
				} {
					^MethodError("bad 'target' argument", this).throw;
				}
			}
		}; // else: broadcast
		oscMsg = ['/sc/msg', groupID, userID, time !? { time.asFloat }, reliable.asBoolean.asInteger ] ++ msg.asOSCArgArray;
		time.notNil.if {
			// schedule on the current (logical) system time.
			// on the Server, we add the relative timestamp contained in the OSC message
			nodeAddr.sendBundle(0, oscMsg);
		} {
			nodeAddr.sendMsg(*oscMsg);
		};
	}

	packetSize { arg size;
		server.sendMsg('/cmd', '/aoo_client_packet_size', this.port, size);
	}

	pingInterval { arg seconds;
		server.sendMsg('/cmd', '/aoo_client_ping', this.port, seconds);
	}

	// Try to find peer, but only if no IP/port is given.
	// So far only called by send().
	prResolveAddr { arg addr;
		var peer;
		addr.ip !? { ^addr; };
		peer = this.prFindPeer(addr);
		peer !? { ^peer; };
		addr.isKindOf(AooPeer).if {
			MethodError("%: couldn't find peer %".format(this.class.name, addr), this).throw;
		} {
			MethodError("%: bad address %".format(this.class.name, addr), this).throw;
		}
	}

	*prNextToken {
		^nextToken = nextToken + 1;
	}

	prSimPacketLoss { arg pct;
		server.sendMsg('/cmd', '/aoo_client_sim_packet_loss', this.port, pct.asFloat);
	}

	prSimPacketReorder { arg sec;
		server.sendMsg('/cmd', '/aoo_client_sim_packet_reorder', this.port, sec.asFloat);
	}

	prSimPacketJitter { arg enable;
		server.sendMsg('/cmd', '/aoo_client_sim_packet_jitter', this.port, enable.asInteger);
	}
}
