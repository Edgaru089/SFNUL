/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#pragma once

#include <SFNUL/Config.hpp>
#include <SFNUL/NetworkResource.hpp>

namespace sfn {

/** Base socket class.
 */
class SFNUL_API Socket : public NetworkResource {

public:
	/** Constructor.
	 */
	Socket();

	/** Destructor.
	 */
	~Socket();
};

}
