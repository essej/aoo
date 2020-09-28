AooSend : UGen {
	var <>desc;
	var <>tag;

	*ar { arg port, id=0, state=0, channels, tag;
		^this.multiNewList([\audio, tag, port, id, state] ++ channels);
	}
	*kr { ^this.shouldNotImplement(thisMethod) }

	init { arg tag ... args;
		this.tag = tag;
		inputs = args;
		^0; // doesn't have any output
	}

	optimizeGraph {
		Aoo.prMakeMetadata(this);
	}

	synthIndex_ { arg index;
		super.synthIndex_(index); // !
		// update metadata (ignored if reconstructing from disk)
		this.desc.notNil.if { this.desc.index = index; }
	}
}

AooSendCtl : AooCtl {
	classvar <>ugenClass;

	*initClass {
		Class.initClassTree(AooSend);
		ugenClass = AooSend;
	}

	prHandleEvent { arg event ... args;

	}

	add { arg host, port, id, channelOnset;
		this.prSendMsg('/add', host, port, id, channelOnset);
	}

	remove { arg host, port, id;
		this.prSendMsg('/remove', host, port, id);
	}

	removeAll {
		this.prSendMsg('/remove');
	}

	accept { arg enable;
		this.prSendMsg('/accept', enable);
	}

	format { arg codec, blockSize, sampleRate ... args;
		this.prSendMsg('/format', codec, blockSize, sampleRate, *args);
	}

	channelOnset { arg host, port, id, onset;
		this.prSendMsg('/channel', host, port, id, onset);
	}

	packetSize_ { arg size;
		this.prSendMsg('/packetsize', size);
	}

	pingInterval_ { arg ms;
		this.prSendMsg('/ping', ms);
	}

	resendBufsize_ { arg ms;
		this.prSendMsg('/resend', ms);
	}

	redundancy_ { arg n;
		this.prSendMsg('/redundancy', n);
	}

	timeFilterBW_ { arg bw;
		this.prSendMsg('/timefilter', bw);
	}
}
