/*
 * lib/string/strncmp.c
 * Copyright (C) 2016 Alexei Frolov
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include <string.h>

int strncmp(const char *s1, const char *s2, size_t n)
{
	for (; n; ++s1, ++s2, --n) {
		if (*s1 != *s2)
			return *s1 < *s2 ? -1 : 1;
		else if (!*s1)
			return 0;
	}
	return 0;
}
