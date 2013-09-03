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



#include <pthread.h>

class CCritSect
{
public:
    CCritSect()
    {
        pthread_mutex_init(&cs, NULL);
        locked = 0;
    }

    ~CCritSect()
    {
         pthread_mutex_destroy (&cs);
    }

    void Lock()
    {
        pthread_mutex_lock (&cs);
        locked = 1;
    }

    void Unlock()
    {
        locked = 0;
        pthread_mutex_unlock (&cs);
    }

    bool IsLocked()
    {
        return (locked != 0);
    }

protected:
    pthread_mutex_t cs;
    int locked;
};

#endif // !defined(CRITSECT_H)

