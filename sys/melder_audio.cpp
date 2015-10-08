/* melder_audio.cpp
 *
 * Copyright (C) 1992-2011,2012,2013,2014,2015 Paul Boersma
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

/*
 * pb 2003/08/22 used getenv ("AUDIODEV") on Sun (thanks to Michel Scheffers)
 * pb 2003/10/22 fake mono for Linux drivers that do not support mono
 * pb 2003/12/06 use sys/soundcard.h instead of linux/soundcard.h for FreeBSD compatibility
 * pb 2004/05/07 removed motif_mac_setNullEventWaitingTime (we nowadays use 1 clock tick everywhere anyway)
 * pb 2004/08/10 fake mono for Linux drivers etc, also if not asynchronous
 * pb 2005/02/13 added O_NDELAY when opening /dev/dsp on Linux (suggestion by Rafael Laboissiere)
 * pb 2005/03/31 undid previous change (four complaints that sound stopped playing)
 * pb 2005/05/19 redid previous change (with fctrl fix suggested by Rafael Laboissiere)
 * pb 2005/10/13 edition for OpenBSD
 * pb 2006/10/28 erased MacOS 9 stuff
 * pb 2006/12/16 Macintosh uses CoreAudio (via PortAudio)
 * pb 2007/01/03 best sample rate can be over 64 kHz
 * pb 2007/05/13 null pointer test for deviceInfo (thanks to Stefan de Konink)
 * pb 2007/08/12 wchar
 * Stefan de Konink 2007/12/02 big-endian Linux
 * pb 2007/12/04 enums
 * pb 2008/06/01 removed SPEXLAB audio server
 * pb 2008/06/10 made PortAudio and foreground playing optional
 * pb 2008/07/03 DirectSound
 * pb 2010/05/09 GTK
 * pb 2011/02/11 better message
 * pb 2011/04/05 C++
 * pb 2015/06/08 char32
 */

#if defined (macintosh)
	#include <sys/time.h>
	#include <math.h>
#elif defined (_WIN32)
	#include <windows.h>
	#include <math.h>
#elif defined (linux)
	#include <sys/time.h>
	#include <signal.h>
	#include <sys/types.h>
	#include <sys/stat.h>
	#include <sys/ioctl.h>
	#include <fcntl.h>
	#include <unistd.h>
	#if defined (__OpenBSD__) || defined (__NetBSD__)
		#include <soundcard.h>
	#else
		#include <sys/soundcard.h>
	#endif
	#include <errno.h>
#endif

#include "melder.h"
#include "Gui.h"
#include "Preferences.h"
#include "NUM.h"
#include <time.h>
#define my  me ->

#include "../external/portaudio/portaudio.h"

static struct {
	enum kMelder_asynchronicityLevel maximumAsynchronicity;
	bool useInternalSpeaker, inputUsesPortAudio, outputUsesPortAudio;
	double silenceBefore, silenceAfter;
} preferences;

void Melder_audio_prefs (void) {
	Preferences_addEnum (U"Audio.maximumAsynchronicity", & preferences. maximumAsynchronicity, kMelder_asynchronicityLevel, kMelder_asynchronicityLevel_DEFAULT);
	Preferences_addBool (U"Audio.useInternalSpeaker", & preferences. useInternalSpeaker, true);
	Preferences_addBool (U"Audio.outputUsesPortAudio2", & preferences. outputUsesPortAudio, kMelderAudio_outputUsesPortAudio_DEFAULT);
	Preferences_addDouble (U"Audio.silenceBefore", & preferences. silenceBefore, kMelderAudio_outputSilenceBefore_DEFAULT);
	Preferences_addDouble (U"Audio.silenceAfter", & preferences. silenceAfter, kMelderAudio_outputSilenceAfter_DEFAULT);
	Preferences_addBool (U"Audio.inputUsesPortAudio2", & preferences. inputUsesPortAudio, kMelderAudio_inputUsesPortAudio_DEFAULT);
}

void MelderAudio_setOutputMaximumAsynchronicity (enum kMelder_asynchronicityLevel maximumAsynchronicity) {
	//MelderAudio_stopPlaying (MelderAudio_IMPLICIT);   // BUG
	preferences. maximumAsynchronicity = maximumAsynchronicity;
}
enum kMelder_asynchronicityLevel MelderAudio_getOutputMaximumAsynchronicity (void) { return preferences. maximumAsynchronicity; }

void MelderAudio_setInputUsesPortAudio (bool inputUsesPortAudio) {
	preferences. inputUsesPortAudio = inputUsesPortAudio;
}
bool MelderAudio_getInputUsesPortAudio (void) { return preferences. inputUsesPortAudio; }

void MelderAudio_setOutputUsesPortAudio (bool outputUsesPortAudio) {
	MelderAudio_stopPlaying (MelderAudio_IMPLICIT);
	preferences. outputUsesPortAudio = outputUsesPortAudio;
}
bool MelderAudio_getOutputUsesPortAudio (void) { return preferences. outputUsesPortAudio; }

void MelderAudio_setUseInternalSpeaker (bool useInternalSpeaker) {
	MelderAudio_stopPlaying (MelderAudio_IMPLICIT);
	preferences. useInternalSpeaker = useInternalSpeaker;
}
bool MelderAudio_getUseInternalSpeaker (void) { return preferences. useInternalSpeaker; }

void MelderAudio_setOutputSilenceBefore (double silenceBefore) {
	MelderAudio_stopPlaying (MelderAudio_IMPLICIT);
	preferences. silenceBefore = silenceBefore;
}
double MelderAudio_getOutputSilenceBefore (void) { return preferences. silenceBefore; }

void MelderAudio_setOutputSilenceAfter (double silenceAfter) {
	MelderAudio_stopPlaying (MelderAudio_IMPLICIT);
	preferences. silenceAfter = silenceAfter;
}
double MelderAudio_getOutputSilenceAfter (void) { return preferences. silenceAfter; }

long MelderAudio_getOutputBestSampleRate (long fsamp) {
	#if defined (macintosh)
		return fsamp == 44100 || fsamp == 96000 ? fsamp : 44100;
	#elif defined (_WIN32)
		return fsamp == 8000 || fsamp == 11025 || fsamp == 16000 || fsamp == 22050 ||
			fsamp == 32000 || fsamp == 44100 || fsamp == 48000 || fsamp == 96000 ? fsamp : 44100;
	#elif defined (linux)
		return fsamp == 44100 || fsamp == 48000 || fsamp == 96000 ? fsamp : 44100;
	#else
		return 44100;
	#endif
}

bool MelderAudio_isPlaying;

static double theStartingTime = 0.0;

static struct MelderPlay {
	int16_t *buffer;
	long sampleRate, numberOfSamples, samplesLeft, samplesSent, samplesPlayed;
	unsigned int asynchronicity;
	int numberOfChannels;
	bool explicitStop, fakeMono;
	volatile int volatile_interrupted;
	bool (*callback) (void *closure, long samplesPlayed);
	void *closure;
	#if cocoa
		CFRunLoopTimerRef cocoaTimer;
	#elif motif
		XtWorkProcId workProcId_motif;
	#elif gtk
		gint workProcId_gtk;
	#endif
	bool usePortAudio, supports_paComplete;
	PaStream *stream;
	double paStartingTime;
	#if defined (macintosh)
	#elif defined (linux)
		int audio_fd, val, err;
	#elif defined (_WIN32)
		HWAVEOUT hWaveOut;
		WAVEHDR waveHeader;
		MMRESULT status;
	#endif
} thePlay;

long MelderAudio_getSamplesPlayed (void) {
	return thePlay. samplesPlayed;
}

bool MelderAudio_stopWasExplicit (void) {
	return thePlay. explicitStop;
}

/*
 * The flush () procedure will always have to be invoked after normal play, i.e. in the following cases:
 * 1. After synchronous play (asynchronicity = 0, 1, or 2).
 * 2. After interruption of asynchronicity 2 by the ESCAPE key.
 * 3. After asynchronous play, by the workProc.
 * 4. After interruption of asynchronicity 3 by MelderAudio_stopPlaying ().
 */
static bool flush (void) {
	struct MelderPlay *me = & thePlay;
	if (my usePortAudio) {
		if (my stream) {
			#ifdef linux

				Pa_Sleep (200);   // this reduces the chance of seeing the Alsa/PulseAudio deadlock:
				/*
					(gdb) thread apply all bt

					Thread 13 (Thread 0x7fffde1d2700 (LWP 25620)):
					#0  0x00007ffff65a3d67 in pthread_cond_wait@@GLIBC_2.3.2 ()
					   from /lib/x86_64-linux-gnu/libpthread.so.0
					#1  0x00007fffec0b3980 in pa_threaded_mainloop_wait () from /usr/lib/x86_64-linux-gnu/libpulse.so.0
					#2  0x00007fffde407054 in pulse_wait_operation ()
					   from /usr/lib/x86_64-linux-gnu/alsa-lib/libasound_module_pcm_pulse.so
					#3  0x00007fffde405c10 in ?? ()
					   from /usr/lib/x86_64-linux-gnu/alsa-lib/libasound_module_pcm_pulse.so
					#4  0x00007ffff6843708 in alsa_snd_pcm_drop () from /usr/lib/x86_64-linux-gnu/libasound.so.2
					#5  0x0000000000812de0 in AlsaStop ()
					#6  0x00000000008183e1 in OnExit ()
					#7  0x0000000000818483 in CallbackThreadFunc ()
					#8  0x00007ffff659fe9a in start_thread () from /lib/x86_64-linux-gnu/libpthread.so.0
					#9  0x00007ffff5aba3fd in clone () from /lib/x86_64-linux-gnu/libc.so.6
					#10 0x0000000000000000 in ?? ()

					Thread 12 (Thread 0x7fffdffff700 (LWP 25619)):
					#0  0x00007ffff659d9b0 in __pthread_mutex_lock_full () from /lib/x86_64-linux-gnu/libpthread.so.0
					#1  0x00007fffdf3d7e1e in pa_mutex_lock () from /usr/lib/x86_64-linux-gnu/libpulsecommon-1.1.so
					#2  0x00007fffec0b3369 in ?? () from /usr/lib/x86_64-linux-gnu/libpulse.so.0
					#3  0x00007fffec0a476c in pa_mainloop_poll () from /usr/lib/x86_64-linux-gnu/libpulse.so.0
					#4  0x00007fffec0a4dd9 in pa_mainloop_iterate () from /usr/lib/x86_64-linux-gnu/libpulse.so.0
					#5  0x00007fffec0a4e90 in pa_mainloop_run () from /usr/lib/x86_64-linux-gnu/libpulse.so.0
					#6  0x00007fffec0b330f in ?? () from /usr/lib/x86_64-linux-gnu/libpulse.so.0
					#7  0x00007fffdf3d8d18 in ?? () from /usr/lib/x86_64-linux-gnu/libpulsecommon-1.1.so
					#8  0x00007ffff659fe9a in start_thread () from /lib/x86_64-linux-gnu/libpthread.so.0
					#9  0x00007ffff5aba3fd in clone () from /lib/x86_64-linux-gnu/libc.so.6
					#10 0x0000000000000000 in ?? ()

					Thread 3 (Thread 0x7fffefd8b700 (LWP 25610)):
					#0  0x00007ffff5aaea43 in poll () from /lib/x86_64-linux-gnu/libc.so.6
					#1  0x00007ffff6ae9ff6 in ?? () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
					#2  0x00007ffff6aea45a in g_main_loop_run () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
					#3  0x00007ffff4bb75e6 in ?? () from /usr/lib/x86_64-linux-gnu/libgio-2.0.so.0
					#4  0x00007ffff6b0b9b5 in ?? () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
					#5  0x00007ffff659fe9a in start_thread () from /lib/x86_64-linux-gnu/libpthread.so.0
					---Type <return> to continue, or q <return> to quit---
					#6  0x00007ffff5aba3fd in clone () from /lib/x86_64-linux-gnu/libc.so.6
					#7  0x0000000000000000 in ?? ()

					Thread 2 (Thread 0x7ffff058c700 (LWP 25609)):
					#0  0x00007ffff5aaea43 in poll () from /lib/x86_64-linux-gnu/libc.so.6
					#1  0x00007ffff6ae9ff6 in ?? () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
					#2  0x00007ffff6aea45a in g_main_loop_run () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
					#3  0x00007ffff059698b in ?? () from /usr/lib/x86_64-linux-gnu/gio/modules/libdconfsettings.so
					#4  0x00007ffff6b0b9b5 in ?? () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
					#5  0x00007ffff659fe9a in start_thread () from /lib/x86_64-linux-gnu/libpthread.so.0
					#6  0x00007ffff5aba3fd in clone () from /lib/x86_64-linux-gnu/libc.so.6
					#7  0x0000000000000000 in ?? ()

					Thread 1 (Thread 0x7ffff7fce940 (LWP 25608)):
					#0  0x00007ffff65a1148 in pthread_join () from /lib/x86_64-linux-gnu/libpthread.so.0
					#1  0x000000000081073e in PaUnixThread_Terminate ()
					#2  0x0000000000818239 in RealStop ()
					#3  0x00000000008182c7 in AbortStream ()
					#4  0x0000000000811ce5 in Pa_CloseStream ()
					#5  0x0000000000753a6d in flush ()
					#6  0x0000000000753dae in MelderAudio_stopPlaying ()
					#7  0x00000000004d80e1 in Sound_playPart ()
					#8  0x00000000004f7b48 in structSoundEditor::v_play ()
					#9  0x00000000004e7197 in gui_drawingarea_cb_click ()
					#10 0x00000000007d0d35 in _GuiGtkDrawingArea_clickCallback ()
					#11 0x00007ffff78d6e78 in ?? () from /usr/lib/x86_64-linux-gnu/libgtk-x11-2.0.so.0
					#12 0x00007ffff6da6ca2 in g_closure_invoke () from /usr/lib/x86_64-linux-gnu/libgobject-2.0.so.0
					#13 0x00007ffff6db7d71 in ?? () from /usr/lib/x86_64-linux-gnu/libgobject-2.0.so.0
					#14 0x00007ffff6dbfd4e in g_signal_emit_valist ()
					   from /usr/lib/x86_64-linux-gnu/libgobject-2.0.so.0
					#15 0x00007ffff6dc0212 in g_signal_emit () from /usr/lib/x86_64-linux-gnu/libgobject-2.0.so.0
					#16 0x00007ffff79f1231 in ?? () from /usr/lib/x86_64-linux-gnu/libgtk-x11-2.0.so.0
					#17 0x00007ffff78d5003 in gtk_propagate_event () from /usr/lib/x86_64-linux-gnu/libgtk-x11-2.0.so.0
					#18 0x00007ffff78d5363 in gtk_main_do_event () from /usr/lib/x86_64-linux-gnu/libgtk-x11-2.0.so.0
					#19 0x00007ffff7549cac in ?? () from /usr/lib/x86_64-linux-gnu/libgdk-x11-2.0.so.0
					#20 0x00007ffff6ae9d13 in g_main_context_dispatch () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
					#21 0x00007ffff6aea060 in ?? () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
					---Type <return> to continue, or q <return> to quit---
					#22 0x00007ffff6aea45a in g_main_loop_run () from /lib/x86_64-linux-gnu/libglib-2.0.so.0
					#23 0x00007ffff78d4397 in gtk_main () from /usr/lib/x86_64-linux-gnu/libgtk-x11-2.0.so.0
					#24 0x00000000007909eb in praat_run ()
					#25 0x000000000040e009 in main ()

					Also see http://sourceforge.net/p/audacity/mailman/audacity-devel/thread/200912181409.49839.businessmanprogrammersteve@gmail.com/
				*/
			#endif
			Pa_CloseStream (my stream);
			my stream = nullptr;
		}
	} else {
	#if defined (macintosh)
	#elif defined (linux)
		/*
		 * As on Sun.
		 */
		if (my audio_fd) {
			ioctl (my audio_fd, SNDCTL_DSP_RESET, (my val = 0, & my val));
			close (my audio_fd), my audio_fd = 0;
		}
	#elif defined (_WIN32)
		/*
		 * FIX: Do not reset the sound card if played to the end:
		 * the last 20 milliseconds may be truncated!
		 * This used to happen on Barbertje's Dell PC, not with SoundBlaster.
		 */
		if (my samplesPlayed != my numberOfSamples || Melder_debug == 2)
			waveOutReset (my hWaveOut);
		my status = waveOutUnprepareHeader (my hWaveOut, & my waveHeader, sizeof (WAVEHDR));
		if (/* Melder_debug == 3 && */ my status == WAVERR_STILLPLAYING) {
			waveOutReset (my hWaveOut);
			waveOutUnprepareHeader (my hWaveOut, & my waveHeader, sizeof (WAVEHDR));
		}
		waveOutClose (my hWaveOut), my hWaveOut = 0;
	#endif
	}
	if (my fakeMono) {
		NUMvector_free ((short *) my buffer, 0);
		my buffer = nullptr;
	}
	MelderAudio_isPlaying = false;
	if (my samplesPlayed >= my numberOfSamples)
		my samplesPlayed = my numberOfSamples;
	if (my samplesPlayed <= 0)
		my samplesPlayed = 1;
	/*
	 * Call the callback for the last time, which is recognizable by the value of MelderAudio_isPlaying.
	 * In this way, the caller of Melder_play16 can be notified.
	 * The caller can examine the actual number of samples played by testing MelderAudio_getSamplesPlayed ().
	 */
	if (my callback)
		my callback (my closure, my samplesPlayed);
	my callback = 0;
	my closure = 0;
	return true;   /* Remove workProc if called from workProc. */
}

bool MelderAudio_stopPlaying (bool explicitStop) {
	//Melder_casual (U"stop playing!");
	struct MelderPlay *me = & thePlay;
	my explicitStop = explicitStop;
	if (! MelderAudio_isPlaying || my asynchronicity < kMelder_asynchronicityLevel_ASYNCHRONOUS) return false;
	#if cocoa
		CFRunLoopRemoveTimer (CFRunLoopGetCurrent (), thePlay. cocoaTimer, kCFRunLoopCommonModes);
	#elif motif
		XtRemoveWorkProc (thePlay. workProcId_motif);
	#elif gtk
		g_source_remove (thePlay. workProcId_gtk);
	#endif
	(void) flush ();
	return true;
}

static bool workProc (void *closure) {
	struct MelderPlay *me = & thePlay;
//static long n = 0;
//n ++;
//Melder_casual (U"workProc ", n);
	if (my usePortAudio) {
		#if defined (linux)
			double timeElapsed = Melder_clock () - theStartingTime - Pa_GetStreamInfo (my stream) -> outputLatency;
			long samplesPlayed = timeElapsed * my sampleRate;
			if (my callback && ! my callback (my closure, samplesPlayed)) {
				my volatile_interrupted = 1;
				return flush ();
			}
			if (my samplesLeft == 0) {
				return flush ();
			}
		#elif defined (linuxXXX)
			/*
			 * Not all hostApis support paComplete or wait till all buffers have been played in Pa_StopStream.
			 * Once pa_win_ds implements this, we can simply do:
			 */
			if (Pa_IsStreamActive (my stream)) {
				if (my callback && ! my callback (my closure, my samplesPlayed))
					return flush ();
			} else {
				Pa_StopStream (my stream);
				my samplesPlayed = my numberOfSamples;
				return flush ();
			}
			/*
			 * But then we also have to use paComplete in the stream callback.
			 */
		#else
			double timeElapsed = Melder_clock () - theStartingTime - Pa_GetStreamInfo (my stream) -> outputLatency;
			my samplesPlayed = (long) floor (timeElapsed * my sampleRate);
			if (my supports_paComplete && Pa_IsStreamActive (my stream)) {
				if (my callback && ! my callback (my closure, my samplesPlayed)) {
					Pa_AbortStream (my stream);
					return flush ();
				}
			} else if (my samplesPlayed < my numberOfSamples + my sampleRate / 20) {   // allow the latency estimate to be 50 ms off.
				if (my callback && ! my callback (my closure, my samplesPlayed)) {
					Pa_AbortStream (my stream);
					return flush ();
				}
			} else {
				Pa_AbortStream (my stream);
				my samplesPlayed = my numberOfSamples;
				return flush ();
			}
			Pa_Sleep (10);
		#endif
	} else {
	#if defined (macintosh)
	#elif defined (linux)
		if (my samplesLeft > 0) {
			int dsamples = my samplesLeft > 500 ? 500 : my samplesLeft;
			write (my audio_fd, (char *) & my buffer [my samplesSent * my numberOfChannels], 2 * dsamples * my numberOfChannels);
			my samplesLeft -= dsamples;
			my samplesSent += dsamples;
			my samplesPlayed = (Melder_clock () - theStartingTime) * my sampleRate;
			if (my callback && ! my callback (my closure, my samplesPlayed))
				return flush ();
		} else /*if (my samplesPlayed >= my numberOfSamples)*/ {
			close (my audio_fd), my audio_fd = 0;
			my samplesPlayed = my numberOfSamples;
			return flush ();
  		/*} else {
			my samplesPlayed = (Melder_clock () - theStartingTime) * my sampleRate;
			if (my callback && ! my callback (my closure, my samplesPlayed))
				return flush ();*/
		}
	#elif defined (_WIN32)
  		if (my waveHeader. dwFlags & WHDR_DONE) {
			my samplesPlayed = my numberOfSamples;
			return flush ();
  		} else {
  			static long previousTime = 0;
  			unsigned long currentTime = clock ();
  			if (Melder_debug == 1) {
				my samplesPlayed = (Melder_clock () - theStartingTime) * my sampleRate;
  			} else {
	  			MMTIME mmtime;
	  			mmtime. wType = TIME_BYTES;
	  			waveOutGetPosition (my hWaveOut, & mmtime, sizeof (MMTIME));
				my samplesPlayed = mmtime. u.cb / (2 * my numberOfChannels);
			}
			if (/* Melder_debug != 4 || */ currentTime - previousTime > CLOCKS_PER_SEC / 100) {
				previousTime = currentTime;
				if (my callback && ! my callback (my closure, my samplesPlayed))
					return flush ();
			}
			Sleep (10);
  		}
	#endif
	}
	(void) closure;
	return false;
}
#if cocoa
static void workProc_cocoa (CFRunLoopTimerRef timer, void *closure) {
	bool result = workProc (closure);
	if (result) {
		CFRunLoopTimerInvalidate (timer);
		//CFRunLoopRemoveTimer (CFRunLoopGetCurrent (), timer);
		
	}
}
#elif motif
static bool workProc_motif (XtPointer closure) {
	return workProc ((void *) closure);
}
#elif gtk
static gint workProc_gtk (gpointer closure) {
	return ! workProc ((void *) closure);
}
#endif

static int thePaStreamCallback (const void *input, void *output,
	unsigned long frameCount,
	const PaStreamCallbackTimeInfo* timeInfo,
	PaStreamCallbackFlags statusFlags,
	void *userData)
{
	(void) input;
	(void) timeInfo;
	(void) userData;
	struct MelderPlay *me = & thePlay;
	if (my volatile_interrupted) {
		memset (output, '\0', 2 * frameCount * my numberOfChannels);
		my samplesPlayed = my numberOfSamples;
		return my supports_paComplete ? paComplete : paContinue;
	}
	if (statusFlags & paOutputUnderflow) {
		if (Melder_debug == 20) Melder_casual (U"output underflow");
	}
	if (statusFlags & paOutputOverflow) {
		if (Melder_debug == 20) Melder_casual (U"output overflow");
	}
	if (my samplesLeft > 0) {
		long dsamples = my samplesLeft > (long) frameCount ? (long) frameCount : my samplesLeft;
		if (Melder_debug == 20) Melder_casual (U"play ", dsamples, U" ", Pa_GetStreamCpuLoad (my stream));
		memset (output, '\0', 2 * frameCount * my numberOfChannels);
		Melder_assert (my buffer);
		memcpy (output, (char *) & my buffer [my samplesSent * my numberOfChannels], 2 * dsamples * my numberOfChannels);
		my samplesLeft -= dsamples;
		my samplesSent += dsamples;
		my samplesPlayed = my samplesSent;
	} else /*if (my samplesPlayed >= my numberOfSamples)*/ {
		memset (output, '\0', 2 * frameCount * my numberOfChannels);
		my samplesPlayed = my numberOfSamples;
		trace (U"paComplete");
		return my supports_paComplete ? paComplete : paContinue;
	}
	return paContinue;
}

void MelderAudio_play16 (int16_t *buffer, long sampleRate, long numberOfSamples, int numberOfChannels,
	bool (*playCallback) (void *playClosure, long numberOfSamplesPlayed), void *playClosure)
{
	struct MelderPlay *me = & thePlay;
	#ifdef _WIN32
		bool wasPlaying = MelderAudio_isPlaying;
	#endif
	if (MelderAudio_isPlaying) MelderAudio_stopPlaying (MelderAudio_IMPLICIT);   // otherwise, keep "explicitStop" tag
	my buffer = buffer;
	my sampleRate = sampleRate;
	my numberOfSamples = numberOfSamples;
	my numberOfChannels = numberOfChannels;
	my callback = playCallback;
	my closure = playClosure;
	my asynchronicity =
		Melder_batch ? kMelder_asynchronicityLevel_SYNCHRONOUS :
		(Melder_backgrounding && ! Melder_asynchronous) ? kMelder_asynchronicityLevel_INTERRUPTABLE :
		kMelder_asynchronicityLevel_ASYNCHRONOUS;
	if (my asynchronicity > preferences. maximumAsynchronicity)
		my asynchronicity = preferences. maximumAsynchronicity;
	trace (U"asynchronicity ", my asynchronicity);
	my usePortAudio = preferences. outputUsesPortAudio;
	my explicitStop = MelderAudio_IMPLICIT;
	my fakeMono = false;
	my volatile_interrupted = 0;

	my samplesLeft = numberOfSamples;
	my samplesSent = 0;
	my samplesPlayed = 0;
	MelderAudio_isPlaying = true;
	if (my usePortAudio) {
		PaError err;
		static bool paInitialized = false;
		if (! paInitialized) {
			err = Pa_Initialize ();
			if (err) Melder_fatal (U"PortAudio does not initialize: ", Melder_peek8to32 (Pa_GetErrorText (err)));
			paInitialized = true;
		}
		my supports_paComplete = Pa_GetHostApiInfo (Pa_GetDefaultHostApi ()) -> type != paDirectSound &&false;
		PaStreamParameters outputParameters = { 0 };
		outputParameters. device = Pa_GetDefaultOutputDevice ();
		const PaDeviceInfo *deviceInfo = Pa_GetDeviceInfo (outputParameters. device);
		trace (U"the device can handle ", deviceInfo -> maxOutputChannels, U" channels");
		if (my numberOfChannels > deviceInfo -> maxOutputChannels) {
			my numberOfChannels = deviceInfo -> maxOutputChannels;
		}
		if (numberOfChannels > my numberOfChannels) {
			/*
			 * Redistribute the in channels over the out channels.
			 */
			if (numberOfChannels == 4 && my numberOfChannels == 2) {   // a common case
				int16_t *in = & my buffer [0], *out = & my buffer [0];
				for (long isamp = 1; isamp <= numberOfSamples; isamp ++) {
					long in1 = *in ++, in2 = *in ++, in3 = *in ++, in4 = *in ++;
					*out ++ = (in1 + in2) / 2;
					*out ++ = (in3 + in4) / 2;
				}
			} else {
				int16_t *in = & my buffer [0], *out = & my buffer [0];
				for (long isamp = 1; isamp <= numberOfSamples; isamp ++) {
					for (long iout = 1; iout <= my numberOfChannels; iout ++) {
						long outValue = 0;
						long numberOfIn = numberOfChannels / my numberOfChannels;
						if (iout == my numberOfChannels)
							numberOfIn += numberOfChannels % my numberOfChannels;
						for (long iin = 1; iin <= numberOfIn; iin ++)
							outValue += *in ++;
						outValue /= numberOfIn;
						*out ++ = outValue;
					}
				}
			}
		}
		outputParameters. channelCount = my numberOfChannels;
		outputParameters. sampleFormat = paInt16;
		if (deviceInfo) outputParameters. suggestedLatency = deviceInfo -> defaultLowOutputLatency;
		outputParameters. hostApiSpecificStreamInfo = nullptr;
		err = Pa_OpenStream (& my stream, nullptr, & outputParameters, my sampleRate, paFramesPerBufferUnspecified,
			paDitherOff, thePaStreamCallback, me);
		if (err) Melder_throw (U"PortAudio cannot open sound output: ", Melder_peek8to32 (Pa_GetErrorText (err)), U".");
		theStartingTime = Melder_clock ();
		err = Pa_StartStream (my stream);
		if (err) Melder_throw (U"PortAudio cannot start sound output: ", Melder_peek8to32 (Pa_GetErrorText (err)), U".");
		my paStartingTime = Pa_GetStreamTime (my stream);
		if (my asynchronicity <= kMelder_asynchronicityLevel_INTERRUPTABLE) {
			for (;;) {
				#if defined (linux)
					/*
					 * This is how PortAudio was designed to work.
					 */
					if (my samplesLeft == 0) {
						my samplesPlayed = my numberOfSamples;
						break;
					}
				#else
					/*
					 * A version that doesn't trust that the stream callback will complete.
					 */
					double timeElapsed = Melder_clock () - theStartingTime - Pa_GetStreamInfo (my stream) -> outputLatency;
					long samplesPlayed = (long) floor (timeElapsed * my sampleRate);
					if (samplesPlayed >= my numberOfSamples + my sampleRate / 20) {
						my samplesPlayed = my numberOfSamples;
						break;
					}
				#endif
				bool interrupted = false;
				if (my asynchronicity != kMelder_asynchronicityLevel_SYNCHRONOUS && my callback &&
					! my callback (my closure, my samplesPlayed))
					interrupted = true;
				/*
				 * Safe operation: only listen to key-down events.
				 * Do this on the lowest level that will work.
				 */
				if (my asynchronicity == kMelder_asynchronicityLevel_INTERRUPTABLE && ! interrupted) {
					#if gtk
						// TODO: implement a reaction to the Escape key
					#elif cocoa
						// TODO: implement a reaction to the Escape key
					#elif defined (macintosh)
						EventRecord event;
						if (EventAvail (keyDownMask, & event)) {
							/*
							* Remove the event, even if it was a different key.
							* Otherwise, the key will block the future availability of the Escape key.
							*/
							FlushEvents (keyDownMask, 0);
							/*
							* Catch Escape and Command-period.
							*/
							if ((event. message & charCodeMask) == 27 ||
								((event. modifiers & cmdKey) && (event. message & charCodeMask) == '.'))
							{
								my explicitStop = MelderAudio_EXPLICIT;
								interrupted = true;
							}
						}
					#elif defined (_WIN32)
						MSG event;
						if (PeekMessage (& event, 0, 0, 0, PM_REMOVE) && event. message == WM_KEYDOWN) {
							if (LOWORD (event. wParam) == VK_ESCAPE) {
								my explicitStop = MelderAudio_EXPLICIT;
								interrupted = true;
							}
						}
					#endif
				}
				if (interrupted) {
					flush ();
					return;
				}
				Pa_Sleep (10);
			}
			if (my samplesPlayed != my numberOfSamples) {
				Melder_fatal (U"Played ", my samplesPlayed, U" instead of ", my numberOfSamples, U" samples.");
			}
			#ifndef linux
				Pa_AbortStream (my stream);
			#endif
		} else /* my asynchronicity == kMelder_asynchronicityLevel_ASYNCHRONOUS */ {
			#if cocoa
				CFRunLoopTimerContext context = { 0, nullptr, nullptr, nullptr, nullptr };
				my cocoaTimer = CFRunLoopTimerCreate (nullptr, CFAbsoluteTimeGetCurrent () + 0.02,
					0.02, 0, 0, workProc_cocoa, & context);
				CFRunLoopAddTimer (CFRunLoopGetCurrent (), my cocoaTimer, kCFRunLoopCommonModes);
			#elif motif
				my workProcId_motif = GuiAddWorkProc (workProc_motif, nullptr);
			#elif gtk
				my workProcId_gtk = g_idle_add (workProc_gtk, nullptr);
			#endif
			return;
		}
		flush ();
		return;
	} else {
		#if defined (macintosh)
		#elif defined (linux)
			try {
				/* Big-endian version added by Stefan de Konink, Nov 29, 2007 */
				#if __BYTE_ORDER == __BIG_ENDIAN
					int fmt = AFMT_S16_BE;
				#else
					int fmt = AFMT_S16_LE;
				#endif
				/* O_NDELAY option added by Rafael Laboissiere, May 19, 2005 */
				if ((my audio_fd = open ("/dev/dsp", O_WRONLY | (Melder_debug == 16 ? 0 : O_NDELAY))) == -1) {
					Melder_throw (errno == EBUSY ? U"Audio device already in use." :
						U"Cannot open audio device.\nPlease switch on PortAudio in Praat's Sound Playing Preferences.");
				}
				fcntl (my audio_fd, F_SETFL, 0);   /* Added by Rafael Laboissiere, May 19, 2005 */
				if (ioctl (my audio_fd, SNDCTL_DSP_SETFMT,   // changed SND_DSP_SAMPLESIZE to SNDCTL_DSP_SETFMT; Stefan de Konink, Nov 29, 2007
					(my val = fmt, & my val)) == -1 ||   // error?
					my val != fmt)   // has sound card overridden our sample size?
				{
					Melder_throw (U"Cannot set sample size to 16 bit.");
				}
				if (ioctl (my audio_fd, SNDCTL_DSP_CHANNELS, (my val = my numberOfChannels, & my val)) == -1 ||   /* Error? */
					my val != my numberOfChannels)   /* Has sound card overridden our number of channels? */
				{
					/*
					 * There is one specific case in which we can work around the current failure,
					 * namely when we are trying to play in mono but the driver of the sound card supports stereo only
					 * and notified us of this by overriding our number of channels.
					 */
					if (my numberOfChannels == 1 && my val == 2) {
						my fakeMono = true;
						int16_t *newBuffer = NUMvector <int16_t> (0, 2 * numberOfSamples - 1);
						for (long isamp = 0; isamp < numberOfSamples; isamp ++) {
							newBuffer [isamp + isamp] = newBuffer [isamp + isamp + 1] = buffer [isamp];
						}
						my buffer = newBuffer;
						my numberOfChannels = 2;
					} else {
						Melder_throw (U"Cannot set number of channels to .", my numberOfChannels, U".");
					}
				}
				if (ioctl (my audio_fd, SNDCTL_DSP_SPEED, (my val = my sampleRate, & my val)) == -1 ||    // error?
					my val != my sampleRate)   // has sound card overridden our sampling frequency?
				{
					Melder_throw (U"Cannot set sampling frequency to ", my sampleRate, U" Hz.");
				}

				theStartingTime = Melder_clock ();
				if (my asynchronicity == kMelder_asynchronicityLevel_SYNCHRONOUS) {
					if (write (my audio_fd, & my buffer [0], 2 * numberOfChannels * numberOfSamples) == -1)
						Melder_throw (U"Cannot write audio output.");
					close (my audio_fd), my audio_fd = 0;   // drain; set to zero in order to notify flush ()
					my samplesPlayed = my numberOfSamples;
				} else if (my asynchronicity <= kMelder_asynchronicityLevel_INTERRUPTABLE) {
					bool interrupted = false;
					while (my samplesLeft && ! interrupted) {
						int dsamples = my samplesLeft > 500 ? 500 : my samplesLeft;
						if (write (my audio_fd, (char *) & my buffer [my samplesSent * my numberOfChannels], 2 * dsamples * my numberOfChannels) == -1)
							Melder_throw (U"Cannot write audio output.");
						my samplesLeft -= dsamples;
						my samplesSent += dsamples;
						my samplesPlayed = (Melder_clock () - theStartingTime) * my sampleRate;
						if (my callback && ! my callback (my closure, my samplesPlayed))
							interrupted = true;
					}
					if (! interrupted) {
						/*
						 * Wait for playing to end.
						 */
						close (my audio_fd), my audio_fd = 0;   // BUG: should do a loop
						my samplesPlayed = my numberOfSamples;
					}
				} else /* my asynchronicity == kMelder_asynchronicityLevel_ASYNCHRONOUS */ {
					#ifndef NO_GRAPHICS
						my workProcId_gtk = g_idle_add (workProc_gtk, nullptr);
					#endif
					return;
				}
				flush ();
				return;
			} catch (MelderError) {
				struct MelderPlay *me = & thePlay;
				if (my audio_fd) close (my audio_fd), my audio_fd = 0;
				MelderAudio_isPlaying = false;
				Melder_throw (U"16-bit audio not played.");
			}
		#elif defined (_WIN32)
			try {
				WAVEFORMATEX waveFormat;
				MMRESULT err;
				waveFormat. wFormatTag = WAVE_FORMAT_PCM;
				waveFormat. nChannels = my numberOfChannels;
				waveFormat. nSamplesPerSec = my sampleRate;
				waveFormat. wBitsPerSample = 16;
				waveFormat. nBlockAlign = my numberOfChannels * waveFormat. wBitsPerSample / 8;
				waveFormat. nAvgBytesPerSec = waveFormat. nBlockAlign * waveFormat. nSamplesPerSec;
				waveFormat. cbSize = 0;
				err = waveOutOpen (& my hWaveOut, WAVE_MAPPER, & waveFormat, 0, 0, CALLBACK_NULL | WAVE_ALLOWSYNC);
				if (err != MMSYSERR_NOERROR) {
					MelderAudio_isPlaying = false;
					if (err == MMSYSERR_ALLOCATED)
						Melder_throw (U"Previous sound is still playing? Should not occur!\n"
							U"Report bug to the author: ", err, U"; wasPlaying: ", wasPlaying, U".");
					if (err == MMSYSERR_BADDEVICEID)
						Melder_throw (U"Cannot play a sound. Perhaps another program is playing a sound at the same time?");
					if (err == MMSYSERR_NODRIVER)
						Melder_throw (U"This computer probably has no sound card.");
					if (err == MMSYSERR_NOMEM)
						Melder_throw (U"Not enough free memory to play any sound at all.");
					if (err == WAVERR_BADFORMAT) {
						if (my numberOfChannels > 2) {
							/*
							 * Retry with 2 channels.
							 */
							my numberOfChannels = 2;
							waveFormat. nChannels = my numberOfChannels;
							waveFormat. nBlockAlign = my numberOfChannels * waveFormat. wBitsPerSample / 8;
							waveFormat. nAvgBytesPerSec = waveFormat. nBlockAlign * waveFormat. nSamplesPerSec;
							err = waveOutOpen (& my hWaveOut, WAVE_MAPPER, & waveFormat, 0, 0, CALLBACK_NULL | WAVE_ALLOWSYNC);
							if (err != MMSYSERR_NOERROR)
								Melder_throw (U"Bad sound format even after reduction to 2 channels? Should not occur! Report bug to the author!");
							MelderAudio_isPlaying = true;
						} else {
							Melder_throw (U"Bad sound format? Should not occur! Report bug to the author!");
						}
					} else {
						Melder_throw (U"Unknown error ", err, U" while trying to play a sound? Report bug to the author!");
					}
				}
				if (numberOfChannels > my numberOfChannels) {
					/*
					 * Redistribute the in channels over the out channels.
					 */
					if (numberOfChannels == 4 && my numberOfChannels == 2) {   // a common case
						int16_t *in = & my buffer [0], *out = & my buffer [0];
						for (long isamp = 1; isamp <= numberOfSamples; isamp ++) {
							long in1 = *in ++, in2 = *in ++, in3 = *in ++, in4 = *in ++;
							*out ++ = (in1 + in2) / 2;
							*out ++ = (in3 + in4) / 2;
						}
					} else {
						int16_t *in = & my buffer [0], *out = & my buffer [0];
						for (long isamp = 1; isamp <= numberOfSamples; isamp ++) {
							for (long iout = 1; iout <= my numberOfChannels; iout ++) {
								long outValue = 0;
								long numberOfIn = numberOfChannels / my numberOfChannels;
								if (iout == my numberOfChannels)
									numberOfIn += numberOfChannels % my numberOfChannels;
								for (long iin = 1; iin <= numberOfIn; iin ++)
									outValue += *in ++;
								outValue /= numberOfIn;
								*out ++ = outValue;
							}
						}
					}
				}

				my waveHeader. dwFlags = 0;
				my waveHeader. lpData = (char *) my buffer;
				my waveHeader. dwBufferLength = my numberOfSamples * 2 * my numberOfChannels;
				my waveHeader. dwLoops = 1;
				my waveHeader. lpNext = nullptr;
				my waveHeader. reserved = 0;
				err = waveOutPrepareHeader (my hWaveOut, & my waveHeader, sizeof (WAVEHDR));
			//waveOutReset (my hWaveOut);
				if (err != MMSYSERR_NOERROR) {
					waveOutClose (my hWaveOut), my hWaveOut = 0;
					MelderAudio_isPlaying = false;
					if (err == MMSYSERR_INVALHANDLE)
						Melder_throw (U"No device? Should not occur!");
					if (err == MMSYSERR_NODRIVER)
						Melder_throw (U"No driver? Should not occur!");
					if (err == MMSYSERR_NOMEM)
						Melder_throw (U"Not enough free memory to play this sound.\n"
							U"Remove some objects, play a shorter sound, or buy more memory.");
					Melder_throw (U"Unknown error ", err, U" while preparing header? Should not occur!");
				}

				err = waveOutWrite (my hWaveOut, & my waveHeader, sizeof (WAVEHDR));
				if (err != MMSYSERR_NOERROR) {
					waveOutReset (my hWaveOut);
					waveOutUnprepareHeader (my hWaveOut, & my waveHeader, sizeof (WAVEHDR));
					waveOutClose (my hWaveOut), my hWaveOut = 0;
					MelderAudio_isPlaying = false;
					Melder_throw (U"Error ", err, U" while writing audio output.");   // BUG: should flush
				}

				theStartingTime = Melder_clock ();
				if (my asynchronicity == kMelder_asynchronicityLevel_SYNCHRONOUS) {
					while (! (my waveHeader. dwFlags & WHDR_DONE)) {
						Sleep (10);
					}
					my samplesPlayed = my numberOfSamples;
				} else if (my asynchronicity <= kMelder_asynchronicityLevel_INTERRUPTABLE) {
					while (! (my waveHeader. dwFlags & WHDR_DONE)) {
						MSG event;
						Sleep (10);
						my samplesPlayed = (Melder_clock () - theStartingTime) * my sampleRate;
						if (my callback && ! my callback (my closure, my samplesPlayed))
							break;
						if (my asynchronicity == kMelder_asynchronicityLevel_INTERRUPTABLE &&
							PeekMessage (& event, 0, 0, 0, PM_REMOVE) && event. message == WM_KEYDOWN)
						{
							if (LOWORD (event. wParam) == VK_ESCAPE) {
								my explicitStop = MelderAudio_EXPLICIT;
								break;
							}
						}
					}
				} else {
					my workProcId_motif = GuiAddWorkProc (workProc_motif, nullptr);
					return;
				}
				flush ();
				return;
			} catch (MelderError) {
				Melder_throw (U"16-bit audio not played.");
			}
		#else
			Melder_throw (U"Cannot play a sound on this computer.\n16-bit audio not played.");
		#endif
	}
}

/* End of file melder_audio.cpp */
