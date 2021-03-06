/**
 * \brief  Dummy random support
 * \author Sebastian Sumpf
 * \date   2015-02-16
 */

/*
 * Copyright (C) 2015-2017 Genode Labs GmbH
 *
 * This file is part of the Genode OS framework, which is distributed
 * under the terms of the GNU Affero General Public License version 3.
 */

#include <util/random.h>


int rumpuser_getrandom_backend(void *buf, Genode::size_t buflen, int flags, Genode::size_t *retp)
{
	*retp = buflen;
	return 0;
}
