/*
 *  Copyright (C) 2002-2013  The DOSBox Team
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#include <string.h>
#include "dosbox.h"
#include "inout.h"
#include "mixer.h"
#include "pic.h"
#include "setup.h"
#include "control.h"

#include "reSID/sid.h"

#define SID_FREQ 1022727
//#define SID_FREQ 985248

static struct {
	SID2* sid;
	Bitu rate;
	Bitu basePort;
	Bitu last_used;
	MixerChannel * chan;
} innova;

static void innova_write(Bitu port,Bitu val,Bitu iolen) {
	if (!innova.last_used) {
		innova.chan->Enable(true);
	}
	innova.last_used=PIC_Ticks;

	Bitu sidPort = port-innova.basePort;
	innova.sid->write(sidPort, val);
}

static Bitu innova_read(Bitu port,Bitu iolen) {
	Bitu sidPort = port-innova.basePort;
	return innova.sid->read(sidPort);
}


static void INNOVA_CallBack(Bitu len) {
	if (!len) return;

	cycle_count delta_t = SID_FREQ*len/innova.rate;
	short* buffer = (short*)MixTemp;
	Bitu bufindex = 0;

	while(delta_t && bufindex != len) {
		bufindex += innova.sid->clock(delta_t, buffer+bufindex, len-bufindex);
	}
	innova.chan->AddSamples_m16(len, buffer);

	if (innova.last_used+5000<PIC_Ticks) {
		innova.last_used=0;
		innova.chan->Enable(false);
	}
}

class INNOVA: public Module_base {
private:
	IO_ReadHandleObject ReadHandler;
	IO_WriteHandleObject WriteHandler;
	MixerObject MixerChan;
public:
	INNOVA(Section* configuration):Module_base(configuration) {
		Section_prop * section=static_cast<Section_prop *>(configuration);
		if(!section->Get_bool("innova")) return;
		innova.rate = section->Get_int("samplerate");
		innova.basePort = section->Get_hex("sidbase");
		sampling_method method = SAMPLE_FAST;
		int m = section->Get_int("quality");
		switch(m) {
		case 1: method = SAMPLE_INTERPOLATE; break;
		case 2: method = SAMPLE_RESAMPLE_FAST; break;
		case 3: method = SAMPLE_RESAMPLE_INTERPOLATE; break;
		}

		LOG_MSG("INNOVA:Initializing Innovation SSI-2001 (SID) emulation...");

		WriteHandler.Install(innova.basePort,innova_write,IO_MB,0x20);
		ReadHandler.Install(innova.basePort,innova_read,IO_MB,0x20);
	
		innova.chan=MixerChan.Install(&INNOVA_CallBack,innova.rate,"INNOVA");

		innova.sid = new SID2;
		innova.sid->set_chip_model(MOS6581);
		innova.sid->enable_filter(true);
		innova.sid->enable_external_filter(true);
		innova.sid->set_sampling_parameters(SID_FREQ, method, innova.rate, -1, 0.97);

		innova.last_used=0;

		LOG_MSG("INNOVA:... finished.");
	}
	~INNOVA(){
		delete innova.sid;
	}
};

static INNOVA* test;

static void INNOVA_ShutDown(Section* sec){
	delete test;
}

void INNOVA_OnReset(Section *sec) {
	if (test == NULL) {
		LOG(LOG_MISC,LOG_DEBUG)("Allocating Innova emulation");
		test = new INNOVA(control->GetSection("innova"));
	}
}

void INNOVA_Init() {
	LOG(LOG_MISC,LOG_DEBUG)("Initializing INNOVA emulation");

	AddExitFunction(AddExitFunctionFuncPair(INNOVA_ShutDown),true);
	AddVMEventFunction(VM_EVENT_RESET,AddVMEventFunctionFuncPair(INNOVA_OnReset));
}

