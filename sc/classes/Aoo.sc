Aoo {
	classvar serverMap;

	classvar <versionMajor=2;
	classvar <versionMinor=0;
	classvar <versionPatch=0;
	classvar <versionString;

	classvar <dispatcher;

	*initClass {
		StartUp.add {
			serverMap = IdentityDictionary();
		};
		ServerQuit.add { |server|
			serverMap[server] = nil;
		};
		versionString = "%.%.%".format(versionMajor, versionMinor, versionPatch);
		dispatcher = AooDispatcher();
	}

	*prGetReplyAddr { arg port, server, action;
		var replyAddr;
		var localAddr = NetAddr.localAddr;
		var nodeMap;
		nodeMap = serverMap[server];
		if (nodeMap.isNil) {
			nodeMap = IdentityDictionary();
			serverMap[server] = nodeMap;
		};
		replyAddr = nodeMap[port];
		if (replyAddr.notNil) {
			action.value(replyAddr);
			^this;
		};

		// block until we get the reply address from the Server
		forkIfNeeded {
			var cond = CondVar(), done = false;

			OSCFunc({ arg msg;
				var success = msg[2].asBoolean;
				success.if {
					replyAddr = NetAddr(server.addr.ip, msg[3].asInteger);
					nodeMap[port] = replyAddr;
				} {
					"Could not get reply address".error;
				};
				done = true;
				cond.signalOne;
			}, '/aoo/register', argTemplate: [port]).oneShot;

			server.sendMsg('/cmd', '/aoo_register',
				port, localAddr.ip, localAddr.port);

			cond.wait { done };

			action.value(replyAddr);
		}
	}

	*prMakeMetadata { arg ugen;
		var metadata, key = ugen.class.asSymbol;
		// For older SC versions, where metadata might be 'nil'
		ugen.synthDef.metadata ?? { ugen.synthDef.metadata = () };
		// Add metadata entry if needed:
		metadata = ugen.synthDef.metadata[key];
		metadata ?? {
			metadata = ();
			ugen.synthDef.metadata[key] = metadata;
		};
		ugen.desc = ( port: ugen.port );
		// There can only be a single AooSend without a tag. In this case, the metadata will contain
		// a (single) item at the pseudo key 'false', see ...
		ugen.tag.notNil.if {
			// check for AOO UGen without tag
			metadata.at(false).notNil.if {
				Error("SynthDef '%' contains multiple % instances - can't omit 'tag' argument!".format(ugen.synthDef.name, ugen.class.name)).throw;
			};
			// check for duplicate tagbol
			metadata.at(ugen.tag).notNil.if {
				Error("SynthDef '%' contains duplicate tag '%'".format(ugen.synthDef.name, ugen.tag)).throw;
			};
			metadata.put(ugen.tag, ugen.desc);
		} {
			// metadata must not contain other AooSend instances!
			(metadata.size > 0).if {
				Error("SynthDef '%' contains multiple % instances - can't omit 'tag' argument!".format(ugen.synthDef.name, ugen.class.name)).throw;
			};
			metadata.put(false, ugen.desc);
		};
	}

	*prFindUGens { arg class, synth, synthDef;
		var desc, metadata, ugens;
		// if the synthDef is nil, we try to get the metadata from the global SynthDescLib
		synthDef.notNil.if {
			metadata = synthDef.metadata;
		} {
			desc = SynthDescLib.global.at(synth.defName);
			desc.isNil.if { MethodError("couldn't find SynthDef '%' in global SynthDescLib!".format(synth.defName), this).throw };
			metadata = desc.metadata; // take metadata from SynthDesc, not SynthDef (SC bug)!
		};
		ugens = metadata[class.name.asSymbol];
		(ugens.size == 0).if { MethodError("SynthDef '%' doesn't contain any % instances!".format(synth.defName, class.name), this).throw; };
		^ugens;
	}

	*prFindMetadata { arg class, synth, tag, synthDef;
		var ugens, desc, info;
		ugens = Aoo.prFindUGens(class, synth, synthDef);
		tag.notNil.if {
			// try to find UGen with given tag
			tag = tag.asSymbol; // !
			desc = ugens[tag];
			desc ?? {
				MethodError("SynthDef '%' doesn't contain an % instance with tag '%'!".format(
					synth.defName, class.name, tag), this).throw;
			};
		} {
			// otherwise just get the first (and only) plugin
			(ugens.size > 1).if {
				MethodError("SynthDef '%' contains more than 1 % - please use the 'tag' argument!".format(synth.defName, class.name), this).throw;
			};
			desc = ugens.asArray[0];
		};
		^desc;
	}
}

AooData {
	classvar typeArray;
	classvar typeMap;

	var <>type;
	var <>data;

	*initClass {
		// idea: allow users to register their own private data types,
		// including conversion functions
		// NB: \int64 is a valid AOO data type, but we currently don't
		// support it on SC because it only has 32-bit integers.
		typeArray = #[
			\raw, \text, \osc, \midi, \fudi, \json, \xml,
			\float32, \float64, \int16, \int32, \int64
		];
		typeMap = typeArray.collectAs({ |name, i| name -> i }, IdentityDictionary);
	}

	*new { arg type, data;
		^super.newCopyArgs(type.asSymbol, data);
	}

	printOn { arg stream;
		stream << this.class.name << "("
		<<* [type, data] << ")";
	}

	asOSCArgArray {
		var bytes;
		var toBytes = #{ |a|
			a.collectAs(_.asInteger, Int8Array)
		};

		// TODO: optimize
		var fromNumber = #{ |data, sel, convert|
			var a = Int8Array[];
			var col = CollStream(a);
			data.do { |value|
				convert !? { value = value.perform(convert) };
				col.perform(sel, value);
			};
			a;
		};

		bytes = switch(type,
			\osc, { data.asRawOSC },
			\text, { toBytes.(data.asString) },
			\fudi, { toBytes.(data.asString) },
			\json, { toBytes.(data.asString) },
			\xml, { toBytes.(data.asString) },
			\midi, { toBytes.(data) },
			\int16, { fromNumber.(data, \putInt16, \asInteger) },
			\int32, { fromNumber.(data, \putInt32, \asInteger) },
			\int64, { "data type 'int64' not supported".warn; nil },
			\float32, { fromNumber.(data, \putFloat) },
			\float64, { fromNumber.(data, \putDouble) },
			\raw, {
				if (data.class != Int8Array) {
					"raw data must be Int8Array!".error; nil
				} { data }
			},
			{ "unknown data type '%'".format(type).warn; nil }
		);

		// convert type name to enum
		^[typeMap[type], bytes ?? []];
	}

	*fromBytes { arg typeID, bytes;
		var type, data;
		var toString = #{ |a|
			a.collectAs(_.asAscii, String);
		};
		// TODO: optimize
		var toNumber = #{ |data, elemSize, sel|
			var col = CollStream(data);
			var count = (data.size / elemSize).asInteger;
			count.collect { col.perform(sel) };
		};

		// HACK so we can just pass OSC arguments without checking
		if (typeID.isNil) { ^nil };

		type = typeArray[typeID];
		if (type.isNil) {
			"unknown type ID (%)".format(typeID).error; ^nil;
		};

		if (bytes.class != Int8Array) {
			"data payload must be Int8Array!".error; ^nil;
		};

		data = switch(type.asSymbol,
			\osc, { this.prParseOSCMsg(bytes) },
			\text, { toString.(bytes) },
			\fudi, { toString.(bytes) },
			\json, { toString.(bytes) },
			\xml, { toString.(bytes) },
			\midi, {
				bytes.collect { |b| if (b < 0) { b + 256 } { b } }
			},
			\int16, { toNumber.(bytes, 2, \getInt16) },
			\int32, { toNumber.(bytes, 4, \getInt32) },
			\int64, { "data type 'int64' not supported".error; ^nil },
			\float32, { toNumber.(bytes, 4, \getFloat) },
			\float64, { toNumber.(bytes, 8, \getDouble) },
			\raw, { bytes },
			{ "ignore data of unknown type '%'".format(type).warn; ^nil }
		);

		^this.new(type, data);
	}

	*prParseOSCMsg { arg msg;
		// TODO: use primitive
		var readString = #{ |col|
			var char, result, rem;
			while { (char = col.next) != 0 } { result = result.add(char) };
			rem = col.pos % 4;
			if (rem > 0) { col.skip(4 - rem) };
			result.collectAs(_.asAscii, String);
		};

		var col = CollStream(msg), addr, args;
		if (col.peek == 35) { "OSC bundles not supported yet".warn; ^nil };
		// read address pattern
		if (col.peek != 47) { "not an OSC message!".error; ^nil };
		addr = readString.(col).asSymbol;
		if (col.peek != 44) { "missing typetag string!".error; ^nil };
		col.next; // skip ','
		// iterate over typetags
		readString.(col).do { |tag|
			var size, blob, rem;
			var a = switch(tag,
				$i, { col.getInt32 },
				$f, { col.getFloat },
				$d, { col.getDouble },
				$s, { readString.(col).asSymbol },
				$S, { readString.(col).asSymbol },
				$b, {
					size = col.getInt32;
					blob = col.nextN(size);
					rem = size % 4;
					if (rem != 0) { col.skip(4 - rem) };
					blob.as(Int8Array);
				},
				$m, { col.nextN(4).as(Int8Array) },
				$c, { col.getInt32.asAscii },
				$r, { col.getInt32 },
				$T, { true },
				$F, { false },
				$N, { nil },
				$I, { inf },
				$[, { tag },
				$], { tag },
				{ "typetag '%' not supported!".warn; ^nil }
			);
			args = args.add(a);
		};
		^[ addr ] ++ args;
	}
}

AooError {
	var <>code;
	var <>message;

	*new { arg code, message;
		^super.newCopyArgs(code, message.asString);
	}

	printOn { arg stream;
		stream << this.class.name << "("
		<< message << ")";
	}
}

AooFormat {
	classvar <>codec = \unknown;

	var <>channels;
	var <>blockSize;
	var <>sampleRate;

	codec { ^this.class.codec }

	asOSCArgArray {
		// replace 'nil' with 'auto' Symbol
		arg array = this.instVarSize.collect { arg i;
			this.instVarAt(i) ? \auto;
		};
		^[ this.codec ] ++ array;
	}

	printOn { arg stream;
		stream << this.class.name << "("
		<<* this.asOSCArgArray[1..] << ")";
	}
}

AooFormatNull : AooFormat {
	classvar <>codec = \null;

	*new { arg channels, blockSize, sampleRate;
		^super.newCopyArgs(channels, blockSize, sampleRate);
	}
}

AooFormatPCM : AooFormat {
	classvar <>codec = \pcm;

	var <>bitDepth;

	*new { arg channels, blockSize, sampleRate, bitDepth = \float32;
		^super.newCopyArgs(channels, blockSize, sampleRate, bitDepth.asSymbol);
	}
}

AooFormatOpus : AooFormat {
	classvar <>codec = \opus;

	var <>applicationType;

	*new { arg channels, blockSize, sampleRate, applicationType = \audio;
		^super.newCopyArgs(channels, blockSize, sampleRate, applicationType.asSymbol);
	}
}

AooAddr {
	var <>ip;
	var <>port;

	*new { arg ip, port;
		^super.newCopyArgs(ip.asString, port.asInteger);
	}

	*resolve { arg hostname, port;
		^this.new(NetAddr(hostname, port).ip, port);
	}

	== { arg that;
		if (that.isKindOf(AooAddr)) {
			^((that.ip == ip) and: { that.port == port });
		}
		^false;
	}

	matchItem { arg item;
		^(item == this);
	}

	hash {
		// only hash the name because 'id' is optional
		^this.name.hash;
	}

	printOn { arg stream;
		stream << this.class.name << "(" <<* [ip, port] << ")";
	}
}

AooGroup {
	var <>name;
	var <>id;
	var <>metadata;
	var <>user; // (for internal use)
	// var <>flags;

	*new { arg name, id, metadata;
		^super.newCopyArgs(name !? _.asString, id !? _.asInteger, metadata);
	}

	== { arg that;
		if (that.isKindOf(AooGroup)) {
			^((that.name == name) or: { that.id == id });
		};
		^false;
	}

	hash {
		// only hash the name because 'id' is optional
		^this.name.hash;
	}

	printOn { arg stream;
		stream << this.class.name << "(" <<* [name] << ")";
	}
}

AooUser {
	var <>name;
	var <>id;
	var <>metadata;
	// var <>flags;

	*new { arg name, id, metadata;
		^super.newCopyArgs(name !? _.asString, id !? _.asInteger, metadata);
	}

	== { arg that;
		if (that.isKindOf(AooUser)) {
			^((that.name == name) or: { that.id == id });
		};
		^false;
	}

	hash {
		^this.instVarHash(#[\name, \id])
	}

	printOn { arg stream;
		stream << this.class.name << "(" <<* [name] << ")";
	}
}

AooPeer : AooAddr {
	var <>group;
	var <>user;

	// group/user can be AooGroup/AooUser or group/user names;
	// addr and port are optional
	*new { arg group, user, addr, port;
		if (group.isKindOf(AooGroup).not) {
			group = AooGroup(group);
		};
		if (user.isKindOf(AooUser).not) {
			user = AooUser(user);
		};
		addr = addr !? _.asString;
		port = port !? _.asInteger;
		^super.newCopyArgs(addr, port, group, user);
	}

	*prFromEvent { arg groupID, userID, groupName, userName, addr, port, metadata;
		^AooPeer(AooGroup(groupName, groupID), AooUser(userName, userID, metadata), addr, port);
	}

	== { arg that;
		if (that.isKindOf(AooPeer)) {
			^((that.group == group) and: { that.user == user });
		};
		if (that.isKindOf(AooAddr)) {
			^((that.ip == ip) and: { that.port == port });
		}
		^false;
	}

	hash {
		^this.instVarHash(#[\ip, \port, \group, \user])
	}

	printOn { arg stream;
		if (group.notNil && user.notNil) {
			stream << this.class.name << "(" <<* [group.name, user.name] << ")";
		} { super.printOn(stream) }
	}
}

AooEndpoint {
	var <>addr;
	var <>id;

	*new { arg addr, id;
		^super.newCopyArgs(addr, id);
	}

	== { arg that;
		if (that.isKindOf(AooEndpoint)) {
			^((that.addr == addr) and: { that.id == id });
		}
		^false;
	}

	matchItem { arg item;
		^(item == this);
	}

	hash {
		^this.instVarHash(#[\addr, \id])
	}

	printOn { arg stream;
		if (addr.isKindOf(AooPeer)) {
			stream << this.class.name << "(" <<* [addr.group.name, addr.user.name, id] << ")";
		} {
			stream << this.class.name << "("<<* [addr.ip, addr.port, id] << ")";
		}
	}
}

AooMessageMatcher : AbstractMessageMatcher {
	var addr;

	*new { arg addr, func;
		^super.newCopyArgs(func, addr);
	}

	value { arg msg, time, testPeer, recvPort;
		if (testPeer == addr) {
			func.value(msg, time, testPeer, recvPort);
		}
	}
}

// This is a wrapping dispatcher that converts (relayed) AOO peer messages to
// actual OSC messages and AooPeer 'addresses' and passes them on to registered
// OSC responders. Every AooClient has its own dispatcher instance that the user
// can pass to OSCFunc (instead of a port number). Alternatively, Aoo.dispatcher
// is a global dispatcher instance that matches all clients.
AooDispatcher : OSCMessageDispatcher {
	var <>client;

	*new { arg client;
		^super.new.init.client_(client);
	}

	wrapFunc {|funcProxy|
		var func, srcID, argTemplate;
		func = funcProxy.func;
		srcID = funcProxy.srcID;
		// ignore recvPort
		argTemplate = funcProxy.argTemplate;
		if (argTemplate.notNil) {
			func = OSCArgsMatcher(argTemplate, func)
		};
		if (srcID.notNil) {
			^AooMessageMatcher(srcID, func);
		} { ^func }
	}

	value { arg msg, time, addr, recvPort;
		var client, peer, port;
		// 2 -> OSC
		if (msg[0] == '/aoo/client/msg' and: { msg[4] == 2 }) {
			port = msg[1];
			if (this.client.notNil) {
				if (port == this.client.port) {
					client = this.client;
				} {
					^this; // not meant for this client
				}
			} {
				// global dispatcher: get client from port argument
				client = AooClient.find(port) ?? {
					"could not find AooClient on port %".format(port).error;
					^this;
				}
			};
			peer = AooPeer.prFromEvent(msg[2], msg[3]);
			peer = client.prFindPeer(peer) ?? {
				"AooClient: received message from unknown peer (group ID: %, user ID: %)".format(msg[2], msg[3]).warn;
				^this;
			};
			AooData.prParseOSCMsg(msg[5]) !? { |oscMsg|
				super.value(oscMsg, time, peer, client);
			}
		}
	}
}

AooCtl {
	classvar nextReplyID=0;
	// public
	var <>synth;
	var <>synthIndex;
	var <>port;

	var eventOSCFunc;
	var eventFuncs;
	var replyAddr;

	*new { arg synth, tag, synthDef;
		var md;
		if (tag.notNil and: { tag.isKindOf(Symbol).not }) {
			MethodError("'tag' must be a Symbol!", this).throw;
		};
		md = Aoo.prFindMetadata(this.ugenClass, synth, tag, synthDef);
		^super.new.prInit(synth, md.index, md.port).init;
	}

	*collect { arg synth, tags, synthDef;
		var value, obj, result = ();
		var ugens = Aoo.prFindUGens(this.ugenClass, synth, synthDef);
		tags.notNil.if {
			tags.do { arg tag;
				if (tag.isKindOf(Symbol).not) {
					MethodError("'tag' must be a Symbol!", this).throw;
				};
				value = ugens.at(tag);
				value.notNil.if {
					obj = super.new.prInit(synth, value.index, value.port).init;
					result.put(tag, obj);
				} { "can't find % with tag %".format(this.name, tag).warn; }
			}
		} {
			// get all plugins, except those without tag (shouldn't happen)
			ugens.pairsDo { arg key, value;
				(key.isKindOf(Symbol)).if {
					obj = super.new.prInit(synth, value.index, value.port).init;
					result.put(key, obj);
				} { "ignoring % without tag".format(this.ugenClass.name).warn; }
			}
		};
		^result;
	}

	isOpen { ^this.port.notNil }

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

	prInit { arg synth, synthIndex, port;
		this.synth = synth;
		this.synthIndex = synthIndex;
		this.port = port;
		eventFuncs = IdentityDictionary();

		// get server reply address and setup event handler
		Aoo.prGetReplyAddr(port, synth.server, { |addr|
			if (addr.notNil) {
				replyAddr = addr;

				// add event listener
				eventOSCFunc = OSCFunc({ arg msg;
					var type = msg[3];
					var args = this.prParseEvent(type, msg[4..]);
					if (args.notNil) {
						eventFuncs[type].value(*args);
						eventFuncs[\event].value(type, *args);
					}
				}, '/aoo/event', addr, argTemplate: [synth.nodeID, synthIndex]);
			}
		});

		synth.onFree {
			this.free;
		};
	}

	free {
		eventOSCFunc.free;
		replyAddr = nil;
		synth = nil;
		port = nil;
	}

	*prNextReplyID {
		^nextReplyID = nextReplyID + 1;
	}

	prSendMsg { arg cmd... args;
		synth.server.listSendMsg(this.prMakeMsg(cmd, *args));
	}

	prMakeMsg { arg cmd ... args;
		^['/u_cmd', synth.nodeID, synthIndex, cmd] ++ args;
	}

	prMakeOSCFunc { arg func, path, replyID;
		^OSCFunc({ arg msg;
			// pass arguments after replyID (first arg is success)
			func.value(msg[4].asBoolean, *msg[5..]);
		}, path, replyAddr, argTemplate: [synth.nodeID, synthIndex, replyID]);
	}

	// Try to find peer, even if IP/port is given.
	prResolveAddr { arg addr;
		var client, peer;
		// find peer by group+user
		client = AooClient.find(this.port);
		if (client.notNil) {
			peer = client.prFindPeer(addr);
			if (peer.notNil) { ^peer };
		};
		// we need at least IP+port
		addr.ip !? { ^addr };
		addr.isKindOf(AooPeer).if {
			MethodError("Aoo: couldn't find peer %".format(addr), this).throw;
		} {
			MethodError("Aoo: bad address %".format(addr), this).throw;
		}
	}
}
