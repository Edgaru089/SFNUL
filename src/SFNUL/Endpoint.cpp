/* This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <SFNUL/Endpoint.hpp>
#include <SFNUL/IpAddress.hpp>
#include <SFNUL/IpAddressImpl.hpp>

namespace sfn {

Endpoint::Endpoint() :
	m_address{} {
}

Endpoint::Endpoint( const IpAddress& address, unsigned short port ) :
	m_address{ address },
	m_port{ port }
{

}

Endpoint::Endpoint( unsigned short port ) :
	m_address{},
	m_port{ port }
{

}

Endpoint::~Endpoint() {

}

IpAddress Endpoint::GetAddress() const {
	return m_address;
}

void Endpoint::SetAddress( const IpAddress& address ) {
	m_address = address;
}

unsigned short Endpoint::GetPort() const {
	return m_port;
}

void Endpoint::SetPort( unsigned short port ) {
	m_port = port;
}

bool operator==( const Endpoint& left, const Endpoint& right ) {
	return ( left.GetPort() == right.GetPort() ) && ( left.GetAddress() == right.GetAddress() );
}

}
