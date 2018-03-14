/*
 * include/radix/assert.h
 * Copyright (C) 2018 Alexei Frolov
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

#ifndef RADIX_ASSERT_H
#define RADIX_ASSERT_H

#if defined(CONFIG_DEBUG) && defined(CONFIG_ASSERT)
#include <radix/compiler.h>

#define assert(x) ((void)((x) || (__assert_fail(#x, __FILE__, __LINE__),0)))

__noreturn void __assert_fail(const char *, const char *, int);
#else
#define assert(x) ((void)0)
#endif /* CONFIG_DEBUG && CONFIG_ASSERT */

#endif /* RADIX_ASSERT_H */
