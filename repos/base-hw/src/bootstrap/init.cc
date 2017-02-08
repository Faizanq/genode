/*
 * \brief   Initialization code for bootstrap
 * \author  Stefan Kalkowski
 * \date    2016-09-22
 */

/*
 * Copyright (C) 2016 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU General Public License version 2.
 */

/* local */
#include <platform.h>

/* base includes */
#include <base/internal/globals.h>
#include <base/internal/unmanaged_singleton.h>

Platform & platform() { return *unmanaged_singleton<Platform>(); }

extern "C" void init() __attribute__ ((noreturn));

extern "C" void init()
{
	Genode::init_log();
	platform().enable_mmu();
	platform().start_core();
}