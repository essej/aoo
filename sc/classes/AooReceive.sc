AooReceive : MultiOutUGen {
	var <>desc;

	*ar { arg port, id=0, numChannels=1, tag;
		^this.multiNewList([\audio, tag, port, id, numChannels]);
	}
	*kr { ^this.shouldNotImplement(thisMethod) }

	init { arg tag, port, id, numChannels;
		this.tag = tag;
		inputs = [port, id];
		^this.initOutputs(numChannels, rate)
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

AooReceiveCtl : AooCtl {
	classvar <>ugenClass;

	*initClass {
		Class.initClassTree(AooReceive);
		ugenClass = AooReceive;
	}

	prHandleEvent { arg event ... args;

	}

	invite { arg host, port, id;
		this.prSendMsg('/invite', host, port, id);
	}

	uninvite { arg host, port, id;
		this.prSendMsg('/uninvite', host, port, id);
	}

	uninviteAll {
		this.prSendMsg('/uninvite');
	}

	packetSize_ { arg size;
		this.prSendMsg('/packetsize', size);
	}

	bufsize_ { arg ms;
		this.prSendMsg('/resend', ms);
	}

	redundancy_ { arg n;
		this.prSendMsg('/redundancy', n);
	}

	timeFilterBW_ { arg bw;
		this.prSendMsg('/timefilter', bw);
	}

	reset { arg host, port, id;
		this.prSendMsg('/reset', host, port, id);
	}

	resetAll {
		this.prSendMsg('/reset');
	}

	resend_ { arg enable;
		this.prSendMsg('/resend', enable);
	}

	resendLimit_ { arg limit;
		this.prSendMsg('/resend_limit', limit);
	}

	resendInterval_ { arg ms;
		this.prSendMsg('/resend_interval', ms);
	}
}
