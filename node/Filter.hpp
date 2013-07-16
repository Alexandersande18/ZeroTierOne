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

#ifndef _ZT_FILTER_HPP
#define _ZT_FILTER_HPP

#include <stdio.h>
#include <string.h>
#include <string>
#include <vector>
#include <utility>
#include <stdexcept>

#include "Mutex.hpp"
#include "Range.hpp"

/* Ethernet frame types that might be relevant to us */
#define ZT_ETHERTYPE_IPV4 0x0800
#define ZT_ETHERTYPE_ARP 0x0806
#define ZT_ETHERTYPE_RARP 0x8035
#define ZT_ETHERTYPE_ATALK 0x809b
#define ZT_ETHERTYPE_AARP 0x80f3
#define ZT_ETHERTYPE_IPX_A 0x8137
#define ZT_ETHERTYPE_IPX_B 0x8138
#define ZT_ETHERTYPE_IPV6 0x86dd

/* IP protocols we might care about */
#define ZT_IPPROTO_ICMP 0x01
#define ZT_IPPROTO_IGMP 0x02
#define ZT_IPPROTO_TCP 0x06
#define ZT_IPPROTO_UDP 0x11
#define ZT_IPPROTO_GRE 0x2f
#define ZT_IPPROTO_ESP 0x32
#define ZT_IPPROTO_AH 0x33
#define ZT_IPPROTO_ICMPV6 0x3a
#define ZT_IPPROTO_OSPF 0x59
#define ZT_IPPROTO_IPIP 0x5e
#define ZT_IPPROTO_IPCOMP 0x6c
#define ZT_IPPROTO_L2TP 0x73
#define ZT_IPPROTO_SCTP 0x84
#define ZT_IPPROTO_FC 0x85
#define ZT_IPPROTO_UDPLITE 0x88
#define ZT_IPPROTO_HIP 0x8b

/* IPv4 ICMP types */
#define ZT_ICMP_ECHO_REPLY 0
#define ZT_ICMP_DESTINATION_UNREACHABLE 3
#define ZT_ICMP_SOURCE_QUENCH 4
#define ZT_ICMP_REDIRECT 5
#define ZT_ICMP_ALTERNATE_HOST_ADDRESS 6
#define ZT_ICMP_ECHO_REQUEST 8
#define ZT_ICMP_ROUTER_ADVERTISEMENT 9
#define ZT_ICMP_ROUTER_SOLICITATION 10
#define ZT_ICMP_TIME_EXCEEDED 11
#define ZT_ICMP_BAD_IP_HEADER 12
#define ZT_ICMP_TIMESTAMP 13
#define ZT_ICMP_TIMESTAMP_REPLY 14
#define ZT_ICMP_INFORMATION_REQUEST 15
#define ZT_ICMP_INFORMATION_REPLY 16
#define ZT_ICMP_ADDRESS_MASK_REQUEST 17
#define ZT_ICMP_ADDRESS_MASK_REPLY 18
#define ZT_ICMP_TRACEROUTE 30
#define ZT_ICMP_MOBILE_HOST_REDIRECT 32
#define ZT_ICMP_MOBILE_REGISTRATION_REQUEST 35
#define ZT_ICMP_MOBILE_REGISTRATION_REPLY 36

/* IPv6 ICMP types */
#define ZT_ICMP6_DESTINATION_UNREACHABLE 1
#define ZT_ICMP6_PACKET_TOO_BIG 2
#define ZT_ICMP6_TIME_EXCEEDED 3
#define ZT_ICMP6_PARAMETER_PROBLEM 4
#define ZT_ICMP6_ECHO_REQUEST 128
#define ZT_ICMP6_ECHO_REPLY 129
#define ZT_ICMP6_MULTICAST_LISTENER_QUERY 130
#define ZT_ICMP6_MULTICAST_LISTENER_REPORT 131
#define ZT_ICMP6_MULTICAST_LISTENER_DONE 132
#define ZT_ICMP6_ROUTER_SOLICITATION 133
#define ZT_ICMP6_ROUTER_ADVERTISEMENT 134
#define ZT_ICMP6_NEIGHBOR_SOLICITATION 135
#define ZT_ICMP6_NEIGHBOR_ADVERTISEMENT 136
#define ZT_ICMP6_REDIRECT_MESSAGE 137
#define ZT_ICMP6_ROUTER_RENUMBERING 138
#define ZT_ICMP6_NODE_INFORMATION_QUERY 139
#define ZT_ICMP6_NODE_INFORMATION_RESPONSE 140
#define ZT_ICMP6_INV_NEIGHBOR_SOLICITATION 141
#define ZT_ICMP6_INV_NEIGHBOR_ADVERTISEMENT 142
#define ZT_ICMP6_MLDV2 143
#define ZT_ICMP6_HOME_AGENT_ADDRESS_DISCOVERY_REQUEST 144
#define ZT_ICMP6_HOME_AGENT_ADDRESS_DISCOVERY_REPLY 145
#define ZT_ICMP6_MOBILE_PREFIX_SOLICITATION 146
#define ZT_ICMP6_MOBILE_PREFIX_ADVERTISEMENT 147
#define ZT_ICMP6_CERTIFICATION_PATH_SOLICITATION 148
#define ZT_ICMP6_CERTIFICATION_PATH_ADVERTISEMENT 149
#define ZT_ICMP6_MULTICAST_ROUTER_ADVERTISEMENT 151
#define ZT_ICMP6_MULTICAST_ROUTER_SOLICITATION 152
#define ZT_ICMP6_MULTICAST_ROUTER_TERMINATION 153
#define ZT_ICMP6_RPL_CONTROL_MESSAGE 155

namespace ZeroTier {

class RuntimeEnvironment;

/**
 * A simple Ethernet frame level filter supporting basic IP port DENY
 */
class Filter
{
public:
	/**
	 * Value returned by etherTypeName, etc. on unknown
	 *
	 * These static methods return precisely this, so a pointer equality
	 * check will work.
	 */
	static const char *const UNKNOWN_NAME;

	/**
	 * A filter rule
	 *
	 * This behaves as an immutable value object.
	 */
	class Rule
	{
	public:
		Rule()
			throw() :
			_etherType(),
			_protocol(),
			_port()
		{
		}

		/**
		 * Construct a new rule
		 *
		 * @param etype Ethernet type or empty range for ANY
		 * @param prot Protocol or empty range for ANY (meaning depends on ethertype, e.g. IP protocol numbers)
		 * @param prt Port or empty range for ANY (only applies to some protocols)
		 */
		Rule(const Range<unsigned int> &etype,const Range<unsigned int> &prot,const Range<unsigned int> &prt)
			throw() :
			_etherType(etype),
			_protocol(prot),
			_port(prt)
		{
		}

		inline const Range<unsigned int> &etherType() const throw() { return _etherType; }
		inline const Range<unsigned int> &protocol() const throw() { return _protocol; }
		inline const Range<unsigned int> &port() const throw() { return _port; }

		/**
		 * Test this rule against a frame
		 *
		 * @param etype Type of ethernet frame
		 * @param data Ethernet frame data
		 * @param len Length of ethernet frame
		 * @return True if rule matches
		 * @throws std::invalid_argument Frame invalid or not parseable
		 */
		bool operator()(unsigned int etype,const void *data,unsigned int len) const
			throw(std::invalid_argument);

		/**
		 * @return Human readable representation of rule
		 */
		std::string toString() const;

		inline bool operator==(const Rule &r) const throw() { return ((_etherType == r._etherType)&&(_protocol == r._protocol)&&(_port == r._port)); }
		inline bool operator!=(const Rule &r) const throw() { return !(*this == r); }
		inline bool operator<(const Rule &r) const
			throw()
		{
			if (_etherType < r._etherType)
				return true;
			else if (_etherType == r._etherType) {
				if (_protocol < r._protocol)
					return true;
				else if (_protocol == r._protocol) {
					if (_port < r._port)
						return true;
				}
			}
			return false;
		}
		inline bool operator>(const Rule &r) const throw() { return (r < *this); }
		inline bool operator<=(const Rule &r) const throw() { return !(r < *this); }
		inline bool operator>=(const Rule &r) const throw() { return !(*this < r); }

	private:
		Range<unsigned int> _etherType;
		Range<unsigned int> _protocol;
		Range<unsigned int> _port;
	};

	/**
	 * Action if a rule matches
	 */
	enum Action
	{
		ACTION_DENY = 1,
		ACTION_ALLOW = 2,
		ACTION_LOG = 3,
		ACTION_UNPARSEABLE = 4
	};

	/**
	 * Entry in filter chain
	 */
	struct Entry
	{
		Entry() {}
		Entry(const Rule &r,const Action &a) :
			rule(r),
			action(a)
		{
		}

		Rule rule;
		Action action;
	};

	Filter() :
		_chain(),
		_chain_m()
	{
	}

	Filter(const Filter &f) :
		_chain(),
		_chain_m()
	{
		Mutex::Lock _l(f._chain_m);
		_chain = f._chain;
	}

	inline Filter &operator=(const Filter &f)
	{
		Mutex::Lock _l1(_chain_m);
		Mutex::Lock _l2(f._chain_m);
		_chain = f._chain;
		return *this;
	}

	/**
	 * Remove all filter entries
	 */
	inline void clear()
	{
		Mutex::Lock _l(_chain_m);
		_chain.clear();
	}

	/**
	 * Append a rule/action pair to this chain
	 *
	 * If an identical rule already exists it is removed and a new entry is
	 * added to the end with the new action. (Two identical rules with the
	 * same action wouldn't make sense.)
	 *
	 * @param r Rule to add
	 * @param a Action if rule matches
	 */
	void add(const Rule &r,const Action &a);

	/**
	 * @return Number of rules in filter chain
	 */
	inline unsigned int length() const
		throw()
	{
		Mutex::Lock _l(_chain_m);
		return _chain.size();
	}

	/**
	 * @return Entry in filter chain or null entry if out of bounds
	 */
	inline Entry operator[](const unsigned int i) const
		throw()
	{
		Mutex::Lock _l(_chain_m);
		if (i < _chain.size())
			return _chain[i];
		return Entry();
	}

	/**
	 * Get a string representation of this filter
	 *
	 * @param sep Separator between filter rules, or NULL for comma (default)
	 * @return Human-readable string
	 */
	std::string toString(const char *sep = (const char *)0) const;

	static const char *etherTypeName(const unsigned int etherType)
		throw();
	static const char *ipProtocolName(const unsigned int ipp)
		throw();
	static const char *icmpTypeName(const unsigned int icmpType)
		throw();
	static const char *icmp6TypeName(const unsigned int icmp6Type)
		throw();

	/**
	 * Match against an Ethernet frame
	 *
	 * Note that ACTION_LOG rules do not terminate rule evaluation and
	 * ACTION_LOG is never returned here as a result. It's primarily for
	 * debugging and rule testing.
	 *
	 * @param _r Runtime environment
	 * @param etherType Ethernet frame type
	 * @param frame Ethernet frame data
	 * @param len Length of frame in bytes
	 * @return Action if matched or ACTION_ALLOW if not matched
	 */
	Action operator()(const RuntimeEnvironment *_r,unsigned int etherType,const void *frame,unsigned int len) const;

private:
	std::vector<Entry> _chain;
	Mutex _chain_m;
};

} // namespace ZeroTier

#endif
