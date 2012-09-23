/*
Copyright (C) 2003 Rice1964

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.

 */

#if !defined(CRITSECT_H)
#define CRITSECT_H

#include <ppc/atomic.h>

class CCritSect {
public:

	CCritSect() {
		lck = 0;
		locked = 0;
	}

	~CCritSect() {
		unlock(&lck);
	}

	void Lock() {
		lock(&lck);
		locked = 1;
	}

	void Unlock() {
		locked = 0;
		unlock(&lck);
	}

	bool IsLocked() {
		return (locked != 0);
	}

protected:
	unsigned int lck;
	int locked;
};

#endif // !defined(CRITSECT_H)

