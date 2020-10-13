AooClient {
	classvar <>clients;

	var <>server;
	var <>port;
	var <>state;
	var <>replyAddr;
	var <>nodeAddr;
	var <>eventHandler;
	var <>dispatcher;
	var <>peers;

	var eventOSCFunc;

	*initClass {
		clients = IdentityDictionary.new;
	}

	*find { arg port;
		^clients[port];
	}

	*new { arg port, server, action;
		^super.new.init(port, server, action);
	}

	init { arg port, server, action;
		this.server = server ?? Server.default;
		this.peers = [];
		this.state = \disconnected;
		this.dispatcher = AooDispatcher(this);

		Aoo.prGetServerAddr(this.server, { arg addr;
			this.replyAddr = addr;
			this.nodeAddr = NetAddr("localhost", port);
			// handle events
			eventOSCFunc = OSCFunc({ arg msg;
				var event = this.prHandleEvent(*msg[2..]);
				this.eventHandler.value(*event);
			}, '/aoo/client/event', addr, argTemplate: [port]);
			// open client on the server
			OSCFunc({ arg msg;
				var success = msg[2].asBoolean;
				success.if {
					this.port = port;
					action.value(this);
				} {
					"couldn't create AooClient on port %: %".format(port, msg[3]).error;
					action.value(nil);
				};
			}, '/aoo/client/new', addr, argTemplate: [port]).oneShot;
			this.server.sendMsg('/cmd', '/aoo_client_new', port);
		});

		ServerQuit.add { this.free };
		CmdPeriod.add { this.free };
	}

	prHandleEvent { arg type ...args;
		// /disconnect, /peer/join, /peer/leave, /error
		var addr;

		type.switch(
			'/disconnect', {
				"disconnected from server".error;
			},
			'/error', {
				"AooClient: % (%)".format(args[1], args[0]).error;
			},
			{
				addr = AooAddr.newCopyArgs(*args);
				type.switch(
					'/peer/join', { this.prAdd(addr) },
					'/peer/leave', { this.prRemove(addr) }
				);
				^[type, addr] // return modified event
			}
		);
		^[type] ++ args; // return original event
	}

	prAdd { arg peer;
		this.peers = this.peers.add(peer);
	}

	prRemove { arg peer;
		var index = this.peers.indexOfEqual(peer);
		index !? { this.peers.removeAt(index) };
	}

	prRemoveGroup { arg group;
		group = group.asSymbol;
		this.peers = this.peers.select { arg p; p.group != group };
	}

	free {
		eventOSCFunc.free;
		port.notNil.if {
			server.sendMsg('/cmd', '/aoo_client_free', port);
		};
		server = nil;
		port = nil;
		peers = nil;
	}

	connect { arg serverName, serverPort, user, pwd, action, timeout=10;
		var resp;

		port ?? { MethodError("AooClient: not initialized", this).throw };

		state.switch(
			\connected, { "AooClient: already connected".warn; ^this },
			\connecting, { "AooClient: still connecting".warn ^this }
		);
		state = \connecting;

		resp = OSCFunc({ arg msg;
			var success = msg[2].asBoolean;
			var errmsg = msg[3];
			success.if {
				"AooClient: connected to % %".format(serverName, serverPort).postln;
				state = \connected;
			} {
				"AooClient: couldn't connect to % %: %".format(serverName, serverPort, errmsg).error;
				state = \disconnected;
			};
			action.value(success, errmsg);
		}, '/aoo/client/connect', replyAddr, argTemplate: [port]).oneShot;

		// NOTE: the default timeout should be larger than the default
		// UDP handshake time out.
		// We need the timeout in case a reply message gets lost and
		// leaves the client in limbo...
		SystemClock.sched(timeout, {
			(state == \connecting).if {
				"AooClient: connection time out".error;
				resp.free;
				state = \disconnected;
				action.value(false, "time out");
			}
		});

		server.sendMsg('/cmd', '/aoo_client_connect', port,
			serverName, serverPort, user, pwd);
	}

	disconnect { arg action;
		port ?? { MethodError("AooClient not initialized", this).throw };

		(state != \connected).if {
			"AooClient not connected".warn;
			^this;
		};
		OSCFunc({ arg msg;
			var success = msg[2].asBoolean;
			var errmsg = msg[3];
			success.if {
				this.peers = []; // remove all peers
				"AooClient: disconnected".postln;
			} {
				"AooClient: couldn't disconnect: %".format(errmsg).error;
			};
			state = \disconnected;
			action.value(success, errmsg);
		}, '/aoo/client/disconnect', replyAddr, argTemplate: [port]).oneShot;
		server.sendMsg('/cmd', '/aoo_client_disconnect', port);
	}

	joinGroup { arg group, pwd, action;
		port ?? { MethodError("AooClient not initialized", this).throw };

		OSCFunc({ arg msg;
			var success = msg[3].asBoolean;
			var errmsg = msg[4];
			success.if {
				"AooClient: joined group '%'".format(group).postln;
			} {
				"AooClient: couldn't join group '%': %".format(group, errmsg).error;
			};
			action.value(success, errmsg);
		}, '/aoo/client/group/join', replyAddr, argTemplate: [port, group.asSymbol]).oneShot;
		server.sendMsg('/cmd', '/aoo_client_group_join', port, group, pwd);
	}

	leaveGroup { arg group, action;
		port ?? { MethodError("AooClient not initialized", this).throw };

		OSCFunc({ arg msg;
			var success = msg[3].asBoolean;
			var errmsg = msg[4];
			success.if {
				"AooClient: left group '%'".format(group).postln;
				this.prRemoveGroup(group);
			} {
				"AooClient: couldn't leave group '%': %".format(group, errmsg).error;
			};
			action.value(success, errmsg);
		}, '/aoo/client/group/leave', replyAddr, argTemplate: [port, group.asSymbol]).oneShot;
		server.sendMsg('/cmd', '/aoo_client_group_leave', port, group);
	}

	findPeer { arg addr;
		addr.ip.notNil.if {
			// find by IP/port
			peers.do { arg peer;
				((peer.ip == addr.ip) && (peer.port == addr.port)).if { ^peer };
			};
		} { addr.group.notNil.if {
			// find by group/user
			peers.do { arg peer;
				((peer.group == addr.group) && (peer.user == addr.user)).if { ^peer };
			};
		}}
		^nil;
	}

	send { arg msg, target;
		var raw, args, newMsg;
		target.notNil.if {
			target.isKindOf(AooAddr).if {
				// peer
				target = this.prResolveAddr(target);
				args = [target.ip, target.port];
			} {
				// group
				args = target.asSymbol;
			};
		}; // else: broadcast
		raw = msg.asRawOSC;
		newMsg = ['/sc/msg', raw, 0] ++ args;
		// newMsg.postln;
		// OSC bundles begin with '#' (ASCII code 35)
		(raw[0] == 35).if {
			// Schedule on the current system time.
			// On the Server, we add the relative
			// timestamp contained in the bundle
			nodeAddr.sendBundle(0, newMsg);
		} {
			// send immediately
			nodeAddr.sendMsg(*newMsg);
		}
	}

	// Try to find peer, but only if no IP/port is given.
	// So far only called by send().
	prResolveAddr { arg addr;
		var peer;
		addr.ip !? { ^addr; };
		peer = this.findPeer(addr);
		peer !? { ^peer; };
		MethodError("%: couldn't find peer %|%".format(this.class.name, addr.group, addr.user), this).throw;
	}
}
