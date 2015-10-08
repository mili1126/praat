/* melder_console.cpp
 *
 * Copyright (C) 1992-2011 Paul Boersma
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

/* pb 2008/02/17
 * pb 2008/10/27 Melder_consoleIsAnsi
 * pb 2011/04/05 C++
 */

#include "melder.h"
#include "NUM.h"
#ifdef _WIN32
	#include <windows.h>
#endif

bool Melder_consoleIsAnsi = false;

void Melder_writeToConsole (const char32 *message, bool useStderr) {
	if (! message) return;
	#if defined (_WIN32)
		(void) useStderr;
		static HANDLE console = nullptr;
		if (! console) {
			console = CreateFileW (L"CONOUT$", GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, 0);
		}
		if (Melder_consoleIsAnsi) {
			size_t n = str32len (message);
			for (long i = 0; i < n; i ++) {
				unsigned int kar = (unsigned short) message [i];
				fputc (kar, stdout);
			}
		//} else if (Melder_consoleIsUtf8) {
			//char *messageA = Melder_peek32to8 (message);
			//fprintf (stdout, "%s", messageA);
		} else {
			WCHAR *messageW = Melder_peek32toW (message);
			WriteConsoleW (console, messageW, wcslen (messageW), nullptr, nullptr);
		}
	#else
		Melder_fwrite32to8 (message, str32len (message), useStderr ? stderr : stdout);
	#endif
}

#if defined (_WIN32) && defined (CONSOLE_APPLICATION)
int main (int argc, char *argvA []);
extern "C" int wmain (int argc, wchar_t *argvW []);
extern "C" int wmain (int argc, wchar_t *argvW []) {
	char **argvA = nullptr;
	if (argc > 0) {
		argvA = NUMvector <char *> (0, argc - 1);
		for (int iarg = 0; iarg < argc; iarg ++) {
			argvA [iarg] = Melder_32to8 (Melder_peekWto32 ((argvW [iarg])));
		}
	}
	return main (argc, argvA);
}
#endif

/* End of file melder_console.cpp */
