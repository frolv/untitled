/*
 * util/rconfig/rconfig.h
 * Copyright (C) 2017 Alexei Frolov
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

#ifndef RCONFIG_H
#define RCONFIG_H

#include <stddef.h>

#define PROGRAM_NAME "rconfig"

struct rconfig_file {
	char                    *name;
	const char              *path;
	size_t                  alloc_size;
	size_t                  num_sections;
	struct rconfig_section  *sections;
};

struct rconfig_section {
	char                    *name;
	size_t                  alloc_size;
	size_t                  num_configs;
	struct rconfig_config   *configs;
};

enum rconfig_config_type {
	RCONFIG_BOOL,
	RCONFIG_INT,
	RCONFIG_OPTIONS,
	RCONFIG_UNKNOWN
};

struct rconfig_config_int_lim {
	int min;
	int max;
};

struct rconfig_option {
	int  val;
	char *desc;
};

struct rconfig_config_options {
	size_t                  alloc_size;
	size_t                  num_options;
	struct rconfig_option   *options;
};

struct rconfig_config {
	char                    identifier[32];
	char                    desc[64];
	int                     type;
	int                     default_val;
	int                     default_set;
	union {
		struct rconfig_config_int_lim lim;
		struct rconfig_config_options opts;
	};
};

void prepare_sections(struct rconfig_file *config);
void add_section(struct rconfig_file *config, char *name);
void add_config(struct rconfig_section *section, char *identifier);
void add_option(struct rconfig_config *conf, int val, char *desc);
void set_config_type(struct rconfig_config *conf, int type);
int verify_config(struct rconfig_file *file, struct rconfig_config *conf);

static inline struct rconfig_config *curr_config(struct rconfig_file *file)
{
	struct rconfig_section *s;

	s = &file->sections[file->num_sections - 1];
	return &s->configs[s->num_configs - 1];
}

void free_rconfig(struct rconfig_file *config);

int exit_status;

#endif /* RCONFIG_H */
