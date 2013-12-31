/*
 * ZeroTier One - Global Peer to Peer Ethernet
 * Copyright (C) 2012-2013  ZeroTier Networks LLC
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
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * --
 *
 * ZeroTier may be used and distributed under the terms of the GPLv3, which
 * are available at: http://www.gnu.org/licenses/gpl-3.0.html
 *
 * If you would like to embed ZeroTier into a commercial application or
 * redistribute it in a modified binary form, please contact ZeroTier Networks
 * LLC. Start here: http://www.zerotier.com/
 */

#include "Peer.hpp"
#include "Switch.hpp"

#include <algorithm>

namespace ZeroTier {

Peer::Peer() :
	_id(),
	_ipv4p(),
	_ipv6p(),
	_lastUsed(0),
	_lastUnicastFrame(0),
	_lastMulticastFrame(0),
	_lastAnnouncedTo(0),
	_vMajor(0),
	_vMinor(0),
	_vRevision(0),
	_latency(0),
	_requestHistoryPtr(0)
{
}

Peer::Peer(const Identity &myIdentity,const Identity &peerIdentity)
	throw(std::runtime_error) :
	_id(peerIdentity),
	_ipv4p(),
	_ipv6p(),
	_lastUsed(0),
	_lastUnicastFrame(0),
	_lastMulticastFrame(0),
	_lastAnnouncedTo(0),
	_vMajor(0),
	_vMinor(0),
	_vRevision(0)
{
	if (!myIdentity.agree(peerIdentity,_key,ZT_PEER_SECRET_KEY_LENGTH))
		throw std::runtime_error("new peer identity key agreement failed");
}

void Peer::onReceive(
	const RuntimeEnvironment *_r,
	Demarc::Port localPort,
	const InetAddress &remoteAddr,
	unsigned int hops,
	uint64_t packetId,
	Packet::Verb verb,
	uint64_t inRePacketId,
	Packet::Verb inReVerb,
	uint64_t now)
{
	if (!hops) { // direct packet
		// Announce multicast LIKEs to peers to whom we have a direct link
		if ((now - _lastAnnouncedTo) >= ((ZT_MULTICAST_LIKE_EXPIRE / 2) - 1000)) {
			_lastAnnouncedTo = now;
			_r->sw->announceMulticastGroups(SharedPtr<Peer>(this));
		}

		// Update last receive info for our direct path
		WanPath *const wp = (remoteAddr.isV4() ? &_ipv4p : &_ipv6p);
		wp->lastReceive = now;
		wp->localPort = ((localPort) ? localPort : Demarc::ANY_PORT);

		// Do things like learn latency or endpoints on OK or ERROR replies
		if (inReVerb != Packet::VERB_NOP) {
			for(unsigned int p=0;p<ZT_PEER_REQUEST_HISTORY_LENGTH;++p) {
				if ((_requestHistory[p].timestamp)&&(_requestHistory[p].packetId == inRePacketId)&&(_requestHistory[p].verb == inReVerb)) {
					_latency = std::min((unsigned int)(now - _requestHistory[p].timestamp),(unsigned int)0xffff);

					// Only learn paths on replies to packets we have sent, otherwise
					// this introduces both an asymmetry problem in NAT-t and a potential
					// reply DOS attack.
					if (!wp->fixed) {
						wp->addr = remoteAddr;
						TRACE("peer %s learned endpoint %s from %s(%s)",address().toString().c_str(),remoteAddr.toString().c_str(),Packet::verbString(verb),Packet::verbString(inReVerb));
					}

					_requestHistory[p].timestamp = 0;
					break;
				}
			}
		}

		// If we get a valid packet with a different address that is not a response
		// to a request, send a PROBE to authenticate this endpoint and determine if
		// it is reachable.
		if ((!wp->fixed)&&(wp->addr != remoteAddr))
			_r->sw->sendPROBE(SharedPtr<Peer>(this),localPort,remoteAddr);
	}

	if (verb == Packet::VERB_FRAME) {
		_lastUnicastFrame = now;
	} else if (verb == Packet::VERB_MULTICAST_FRAME) {
		_lastMulticastFrame = now;
	}
}

Demarc::Port Peer::send(const RuntimeEnvironment *_r,const void *data,unsigned int len,uint64_t now)
{
	if ((_ipv6p.isActive(now))||((!(_ipv4p.addr))&&(_ipv6p.addr))) {
		if (_r->demarc->send(_ipv6p.localPort,_ipv6p.addr,data,len,-1)) {
			_ipv6p.lastSend = now;
			return _ipv6p.localPort;
		}
	}

	if (_ipv4p.addr) {
		if (_r->demarc->send(_ipv4p.localPort,_ipv4p.addr,data,len,-1)) {
			_ipv4p.lastSend = now;
			return _ipv4p.localPort;
		}
	}

	return Demarc::NULL_PORT;
}

bool Peer::sendFirewallOpener(const RuntimeEnvironment *_r,uint64_t now)
{
	bool sent = false;
	if (_ipv4p.addr) {
		if (_r->demarc->send(_ipv4p.localPort,_ipv4p.addr,"\0",1,ZT_FIREWALL_OPENER_HOPS)) {
			_ipv4p.lastFirewallOpener = now;
			sent = true;
		}
	}
	if (_ipv6p.addr) {
		if (_r->demarc->send(_ipv6p.localPort,_ipv6p.addr,"\0",1,ZT_FIREWALL_OPENER_HOPS)) {
			_ipv6p.lastFirewallOpener = now;
			sent = true;
		}
	}
	return sent;
}

bool Peer::sendPing(const RuntimeEnvironment *_r,uint64_t now)
{
	bool sent = false;
	if (_ipv4p.addr) {
		if (_r->sw->sendHELLO(SharedPtr<Peer>(this),_ipv4p.localPort,_ipv4p.addr)) {
			_ipv4p.lastSend = now;
			sent = true;
		}
	}
	if (_ipv6p.addr) {
		if (_r->sw->sendHELLO(SharedPtr<Peer>(this),_ipv6p.localPort,_ipv6p.addr)) {
			_ipv6p.lastSend = now;
			sent = true;
		}
	}
	return sent;
}

void Peer::setPathAddress(const InetAddress &addr,bool fixed)
{
	if (addr.isV4()) {
		_ipv4p.addr = addr;
		_ipv4p.fixed = fixed;
	} else if (addr.isV6()) {
		_ipv6p.addr = addr;
		_ipv6p.fixed = fixed;
	}
}

void Peer::clearFixedFlag(InetAddress::AddressType t)
{
	switch(t) {
		case InetAddress::TYPE_NULL:
			_ipv4p.fixed = false;
			_ipv6p.fixed = false;
			break;
		case InetAddress::TYPE_IPV4:
			_ipv4p.fixed = false;
			break;
		case InetAddress::TYPE_IPV6:
			_ipv6p.fixed = false;
			break;
	}
}

} // namespace ZeroTier
