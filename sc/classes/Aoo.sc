Aoo {
	classvar serverMap;

	*initClass {
		StartUp.add {
			serverMap = IdentityDictionary.new;
		}
	}

	*prMakeAddr {
		^{ arg break;
			var addr, port = rrand(49152, 65535);
			thisProcess.openUDPPort(port).if {
				addr = NetAddr(NetAddr.localAddr.ip, port);
				break.value(addr);
			};
		}.block;
	}

	*prGetServerAddr { arg server, action;
		var clientID, local, remote;
		remote = serverMap.at(server);
		remote.notNil.if {
			action.value(remote);
		} {
			local = NetAddr.localAddr;
			clientID = UniqueID.next;

			OSCFunc({ arg msg, time, addr;
				"client registered by %:%".format(addr.ip, addr.port).postln;
				serverMap.put(server, addr);
				action.value(addr);
			}, '/aoo/register', argTemplate: [clientID]).oneShot;

			server.sendMsg('/cmd', '/aoo_register',
				local.ip, local.port, clientID);
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
		ugen.desc = ( port: ugen.port, id: ugen.id );
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

	*prResolveAddr { arg port, addr;
		var peer, client = AooClient.find(port);
		client.notNil.if {
			peer = client.findPeer(addr);
			peer !? { ^peer };
		};
		addr.ip !? { ^addr };
		Error("Aoo: couldn't find peer %|%".format(addr.group, addr.user)).throw;
	}
}

AooAddr {
	var <>ip;
	var <>port;
	var <>group;
	var <>user;
	var <>id;

	*new { arg ip, port;
		^super.newCopyArgs(ip.asSymbol, port.asInteger);
	}

	*peer { arg group, user;
		^this.newCopyArgs(nil, nil, group.asSymbol, user.asSymbol);
	}

	== { arg that;
		^this.compareObject(that, #[\ip, \port])
	}

	hash {
		^this.instVarHash(#[\ip, \port])
	}

	printOn { arg stream;
		stream << this.class.name << "("
		<<* [ip, port, group, user, id] << ")";
	}
}

AooCtl {
	classvar nextReplyID=0;
	// public
	var <>synth;
	var <>synthIndex;
	var <>port;
	var <>id;
	var <>replyAddr;
	var <>eventHandler;

	var eventOSCFunc;

	*new { arg synth, tag, synthDef, action;
		var data = Aoo.prFindMetadata(this.ugenClass, synth, tag, synthDef);
		var result = super.new.init(synth, data.index, data.port, data.id);
		action !? {
			forkIfNeeded {
				synth.server.sync;
				action.value(result);
			};
		};
		^result;
	}

	*collect { arg synth, tags, synthDef, action;
		var result = ();
		var ugens = Aoo.prFindUGens(this.ugenClass, synth, synthDef);
		tags.notNil.if {
			tags.do { arg key;
				var value;
				key = key.asSymbol; // !
				value = ugens.at(key);
				value.notNil.if {
					result.put(key, this.new(synth, value.index));
				} { "can't find % with tag %".format(this.name, key).warn; }
			}
		} {
			// get all plugins, except those without tag (shouldn't happen)
			ugens.pairsDo { arg key, value;
				(key.class == Symbol).if {
					result.put(key, this.new(synth, value.index, value.port, value.id));
				} { "ignoring % without tag".format(this.ugenClass.name).warn; }
			}
		};
		action !? {
			forkIfNeeded {
				synth.server.sync;
				action.value(result);
			};
		};
		^result;
	}

	init { arg synth, synthIndex, port, id;
		this.synth = synth;
		this.synthIndex = synthIndex;
		this.port = port;
		this.id = id;

		Aoo.prGetServerAddr(synth.server, { arg addr;
			"listen: % % %".format(addr, synth.nodeID, synthIndex).postln;
			replyAddr = addr;
			eventOSCFunc = OSCFunc({ arg msg;
				var event = this.prHandleEvent(*msg[3..]);
				this.eventHandler.value(*event);
			}, '/aoo/event', addr, argTemplate: [synth.nodeID, synthIndex]);
		});

		synth.onFree {
			this.free;
		};
	}

	free {
		eventOSCFunc.free;
		synth = nil;
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
			func.value(*msg[4..]); // pass arguments after replyID
		}, path, replyAddr, argTemplate: [synth.nodeID, synthIndex, replyID]);
	}
}
