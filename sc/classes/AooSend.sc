AooSend : UGen {
	var <>desc; // reference to desc in SynthDef metadata
	var <>tag;
	var <>port;

	*ar { arg port, id=0, channels, gate=0, tag;
		^this.multiNewList([\audio, tag, port, id, gate] ++ channels);
	}
	*kr { ^this.shouldNotImplement(thisMethod) }

	init { arg tag, port, id, gate ... inputs;
		if (tag.notNil and: { tag.isKindOf(Symbol).not }) {
			MethodError("'tag' must be a Symbol!", this).throw;
		};
		if (port.isInteger.not) {
			MethodError("'port' must be an Integer!", this).throw;
		};
		this.tag = tag !? { tag.asSymbol }; // !
		this.port = port;
		this.inputs = [port, id, gate, inputs.size] ++ inputs;
		^0; // doesn't have any output
	}

	optimizeGraph {
		Aoo.prMakeMetadata(this);
	}

	synthIndex_ { arg index;
		super.synthIndex_(index); // !
		// update metadata (ignored if reconstructing from disk)
		this.desc !? { |desc| desc.index = index };
	}
}

AooSendCtl : AooCtl {
	classvar <>ugenClass;

	var <>sinks;
	var <>format;

	*initClass {
		Class.initClassTree(AooSend);
		ugenClass = AooSend;
	}

	init {
		sinks = [];
	}

	free {
		super.free;
		sinks = nil;
	}

	prParseEvent { arg type, args;
		// \invite, \uninvite, \add, \remove, \ping
		// currently, all events start with AOO endpoint
		var addr = this.prResolveAddr(AooAddr(args[0], args[1]));
		var id = args[2];
		var sink = AooEndpoint(addr, id);
		var event = [sink];
		// If sink doesn't exist, fake an \add event.
		// This happens if the sink has been added
		// automatically before we could create the controller.
		if (this.prFindSink(sink).isNil and: { type != \add }) {
			this.prAddSink(sink);
			this.eventHandler.value(\add, event);
		};
		type.switch(
			\add, { this.prAddSink(sink); ^event },
			\remove, { this.prRemoveSink(sink); ^event },
			\invite, { ^event ++ args[3] ++ AooData.fromBytes(*args[4..]) },
			\uninvite, { ^event ++ args[3] },
			\ping, { ^event ++ args[3..] },
			\frameResent, { ^event ++ args[3] },
			{ "%: ignore unknown event '%'".format(this.class, type).warn; ^nil }
		)
	}

	addSink { arg addr, id, active=true, channel=0, action;
		var replyID = AooCtl.prNextReplyID;
		addr = this.prResolveAddr(addr);

		this.prMakeOSCFunc({ arg success, ip, port, id;
			var newAddr, sink;
			success.if {
				newAddr = this.prResolveAddr(AooAddr(ip, port));
				sink = AooEndpoint(newAddr, id);
				this.prAddSink(sink);
				action.value(sink);
			} { action.value(nil) }
		}, '/aoo/add', replyID).oneShot;

		this.prSendMsg('/add', replyID, addr.ip, addr.port, id, active.asInteger, channel.asInteger);
	}

	removeSink { arg addr, id, action;
		var replyID = AooCtl.prNextReplyID;
		addr = this.prResolveAddr(addr);

		this.prMakeOSCFunc({ arg success;
			action.value(success);
		}, '/aoo/remove', replyID).oneShot;

		this.prSendMsg('/remove', replyID, addr.ip, addr.port, id);
	}

	prAddSink { arg sink;
		this.sinks = this.sinks.add(sink);
	}

	prRemoveSink { arg sink;
		var index = this.sinks.indexOfEqual(sink);
		index !? { this.sinks.removeAt(index) };
	}

	prFindSink { arg sink;
		var index = this.sinks.indexOfEqual(sink);
		^index !? { this.sinks[index] };
	}

	removeAll { arg action;
		var replyID = AooCtl.prNextReplyID;

		this.prMakeOSCFunc({
			this.sinks = [];
			action.value;
		}, '/aoo/remove', replyID).oneShot;

		this.prSendMsg('/remove', replyID);
	}

	handleInvite { arg sink, token, accept=true;
		// var addr = this.prResolveAddr(sink.addr);
		var addr = sink.addr;
		this.prSendMsg('/invite', addr.ip, addr.port, sink.id, token, accept.asInteger);
	}

	handleUninvite { arg sink, token, accept=true;
		// var addr = this.prResolveAddr(sink.addr);
		var addr = sink.addr;
		this.prSendMsg('/uninvite', addr.ip, addr.port, sink.id, token, accept.asInteger);
	}

	autoInvite { arg enable;
		this.prSendMsg('/auto_invite', enable);
	}

	start { arg metadata;
		if (metadata.notNil) {
			this.prSendMsg('/start', *metadata.asOSCArgArray);
		} {
			this.prSendMsg('/start', $N, $N);
		}
	}

	stop {
		this.prSendMsg('/stop');
	}

	activateSink { arg addr, id, active;
		addr = this.prResolveAddr(addr);
		this.prSendMsg('/activate', addr.ip, addr.port, id, active.asBoolean.asInteger)
	}

	setFormat { arg format, action;
		var replyID = AooCtl.prNextReplyID;
		format.isKindOf(AooFormat).not.if {
			MethodError("%: bad type for 'format' argument", this.class.name).throw;
		};
		this.prMakeOSCFunc({ arg success, codec ...args;
			var f;
			if (success) {
				f = codec.switch(
					\pcm, { AooFormatPCM(*args) },
					\opus, { AooFormatOpus(*args) },
					{ "%: unknown format '%'".format(this.class.name, codec).error; nil }
				);
			};
			this.format = f;
			action.value(f);
		}, '/aoo/format', replyID).oneShot;
		this.prSendMsg('/format', replyID, *format.asOSCArgArray);
	}

	setCodecParam { arg param, value;
		if (this.format.isNil) {
			MethodError("%: cannot set codec parameter without format", this.class.name).throw;
		};
		this.prSendMsg('/codec_set', this.format.codec, param, value);
	}

	getCodecParam { arg param, action;
		var replyID = AooCtl.prNextReplyID;

		if (this.format.isNil) {
			MethodError("%: cannot get codec parameter without format", this.class.name).throw;
		};

		this.prMakeOSCFunc({ arg success, codec, param, value;
			if (success) { action.value(value) }
			{ action.value(nil) }
		}, '/aoo/codec/get', replyID).oneShot;

		this.prSendMsg('/codec_get', replyID, this.format.codec, param);
	}

	channelOffset { arg addr, id, offset;
		addr = this.prResolveAddr(addr);
		this.prSendMsg('/channel_offset', addr.ip, addr.port, id, offset);
	}

	packetSize { arg size;
		this.prSendMsg('/packet_size', size);
	}

	pingInterval { arg seconds;
		this.prSendMsg('/ping', seconds);
	}

	resendBufferSize { arg seconds;
		this.prSendMsg('/resend', seconds);
	}

	redundancy { arg count;
		this.prSendMsg('/redundancy', count);
	}

	dynamicResampling { arg enable;
		this.prSendMsg('/dynamic_resampling', enable);
	}

	dllBandwidth { arg bandwidth;
		this.prSendMsg('/dll_bw', bandwidth);
	}
}
