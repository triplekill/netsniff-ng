/*
 * netsniff-ng - the packet sniffing beast
 * Copyright 2014 Tobias Klauser.
 * Subject to the GPL, version 2.
 */

#include <stdio.h>
#include <unistd.h>
#include <limits.h>
#include <libgen.h>
#include <netlink/msg.h>
#include <netlink/route/link.h>
#include <netlink/route/addr.h>
#include <linux/if_arp.h>
#include <arpa/inet.h>

#include "pkt_buff.h"
#include "proto.h"
#include "protos.h"

#define INFINITY 0xFFFFFFFFU

#define RTA_LEN(attr) RTA_PAYLOAD(attr)
#define RTA_INT(attr) (*(int *)RTA_DATA(attr))
#define RTA_UINT8(attr) (*(uint8_t *)RTA_DATA(attr))
#define RTA_STR(attr) ((char *)RTA_DATA(attr))

#define attr_fmt(attr, fmt, ...) \
	tprintf("\tA: "fmt, ##__VA_ARGS__); \
	tprintf(", Len %u\n", RTA_LEN(attr));

static const char *nlmsg_family2str(uint16_t family)
{
	switch (family) {
	case NETLINK_ROUTE:		return "routing";
	case NETLINK_UNUSED:		return "unused";
	case NETLINK_USERSOCK:		return "user-mode socket";
	case NETLINK_FIREWALL:		return "unused, formerly ip_queue";
/* NETLINK_INET_DIAG was renamed to NETLINK_SOCK_DIAG in Linux kernel 3.10 */
#if defined(NETLINK_SOCK_DIAG)
	case NETLINK_SOCK_DIAG:		return "socket monitoring";
#elif defined(NETLINK_INET_DIAG)
	case NETLINK_INET_DIAG:		return "INET socket monitoring";
#endif
	case NETLINK_NFLOG:		return "netfilter ULOG";
	case NETLINK_XFRM:		return "IPsec";
	case NETLINK_SELINUX:		return "SELinux event notification";
	case NETLINK_ISCSI:		return "Open-iSCSI";
	case NETLINK_AUDIT:		return "auditing";
	case NETLINK_FIB_LOOKUP:	return "FIB lookup";
	case NETLINK_CONNECTOR:		return "Kernel connector";
	case NETLINK_NETFILTER:		return "Netfilter";
	case NETLINK_IP6_FW:		return "unused, formerly ip6_queue";
	case NETLINK_DNRTMSG:		return "DECnet routing";
	case NETLINK_KOBJECT_UEVENT:	return "Kernel messages";
	case NETLINK_GENERIC:		return "Generic";
	case NETLINK_SCSITRANSPORT:	return "SCSI transports";
	case NETLINK_ECRYPTFS:		return "ecryptfs";
	case NETLINK_RDMA:		return "RDMA";
	case NETLINK_CRYPTO:		return "Crypto layer";
	default:			return "Unknown";
	}
}

static const char *nlmsg_rtnl_type2str(uint16_t type)
{
	switch (type) {
	case RTM_NEWLINK:	return "new link";
	case RTM_DELLINK:	return "del link";
	case RTM_GETLINK:	return "get link";
	case RTM_SETLINK:	return "set link";

	case RTM_NEWADDR:	return "new addr";
	case RTM_DELADDR:	return "del addr";
	case RTM_GETADDR:	return "get addr";

	case RTM_NEWROUTE:	return "new route";
	case RTM_DELROUTE:	return "del route";
	case RTM_GETROUTE:	return "get route";

	case RTM_NEWNEIGH:	return "new neigh";
	case RTM_DELNEIGH:	return "del neigh";
	case RTM_GETNEIGH:	return "get neigh";

	case RTM_NEWRULE:	return "new rule";
	case RTM_DELRULE:	return "del rule";
	case RTM_GETRULE:	return "get rule";

	case RTM_NEWQDISC:	return "new tc qdisc";
	case RTM_DELQDISC:	return "del tc qdisc";
	case RTM_GETQDISC:	return "get tc qdisc";

	case RTM_NEWTCLASS:	return "new tc class";
	case RTM_DELTCLASS:	return "del tc class";
	case RTM_GETTCLASS:	return "get tc class";

	case RTM_NEWTFILTER:	return "new tc filter";
	case RTM_DELTFILTER:	return "del tc filter";
	case RTM_GETTFILTER:	return "get tc filter";

	case RTM_NEWACTION:	return "new tc action";
	case RTM_DELACTION:	return "del tc action";
	case RTM_GETACTION:	return "get tc action";

	case RTM_NEWPREFIX:	return "new prefix";

	case RTM_GETMULTICAST:	return "get mcast addr";

	case RTM_GETANYCAST:	return "get anycast addr";

	case RTM_NEWNEIGHTBL:	return "new neigh table";
	case RTM_GETNEIGHTBL:	return "get neigh table";
	case RTM_SETNEIGHTBL:	return "set neigh table";

	case RTM_NEWNDUSEROPT:	return "new ndisc user option";

	case RTM_NEWADDRLABEL:	return "new addr label";
	case RTM_DELADDRLABEL:	return "del addr label";
	case RTM_GETADDRLABEL:	return "get addr label";

	case RTM_GETDCB:	return "get data-center-bridge";
	case RTM_SETDCB:	return "set data-center-bridge";

#if defined(RTM_NEWNETCONF)
	case RTM_NEWNETCONF:	return "new netconf";
	case RTM_GETNETCONF:	return "get netconf";
#endif

#if defined(RTM_NEWMDB)
	case RTM_NEWMDB:	return "new bridge mdb";
	case RTM_DELMDB: 	return "del bridge mdb";
	case RTM_GETMDB: 	return "get bridge mdb";
#endif
	default:		return NULL;
	}
}

static char *if_type2str(uint16_t type)
{
	switch (type) {
	case ARPHRD_ETHER: return "ether";
	case ARPHRD_EETHER: return "eether";
	case ARPHRD_AX25: return "ax25";
	case ARPHRD_PRONET: return "pronet";
	case ARPHRD_CHAOS: return "chaos";
	case ARPHRD_IEEE802: return "ieee802";
	case ARPHRD_ARCNET: return "arcnet";
	case ARPHRD_APPLETLK: return "appletlk";
	case ARPHRD_DLCI: return "dlci";
	case ARPHRD_ATM: return "atm";
	case ARPHRD_METRICOM: return "metricom";
	case ARPHRD_IEEE1394: return "ieee1394";
	case ARPHRD_INFINIBAND: return "infiniband";
	case ARPHRD_SLIP: return "slip";
	case ARPHRD_CSLIP: return "cslip";
	case ARPHRD_SLIP6: return "slip6";
	case ARPHRD_CSLIP6: return "cslip6";
	case ARPHRD_RSRVD: return "RSRVD";
	case ARPHRD_ADAPT: return "adapt";
	case ARPHRD_ROSE: return "rose";
	case ARPHRD_X25: return "x25";
	case ARPHRD_HWX25: return "hwx25";
	case ARPHRD_CAN: return "can";
	case ARPHRD_PPP: return "ppp";
	case ARPHRD_HDLC: return "hdlc";
	case ARPHRD_LAPB: return "lapb";
	case ARPHRD_DDCMP: return "ddcmp";
	case ARPHRD_RAWHDLC: return "rawhdlc";
	case ARPHRD_TUNNEL: return "tunnel";
	case ARPHRD_TUNNEL6: return "tunnel6";
	case ARPHRD_FRAD: return "frad";
	case ARPHRD_SKIP: return "skip";
	case ARPHRD_LOOPBACK: return "loopback";
	case ARPHRD_LOCALTLK: return "localtlk";
	case ARPHRD_FDDI: return "fddi";
	case ARPHRD_BIF: return "bif";
	case ARPHRD_SIT: return "sit";
	case ARPHRD_IPDDP: return "ipddp";
	case ARPHRD_IPGRE: return "ipgre";
	case ARPHRD_PIMREG: return "pimreg";
	case ARPHRD_HIPPI: return "hippi";
	case ARPHRD_ASH: return "ash";
	case ARPHRD_ECONET: return "econet";
	case ARPHRD_IRDA: return "irda";
	case ARPHRD_FCPP: return "fcpp";
	case ARPHRD_FCAL: return "fcal";
	case ARPHRD_FCPL: return "fcpl";
	case ARPHRD_FCFABRIC: return "fcfb0";
	case ARPHRD_FCFABRIC + 1: return "fcfb1";
	case ARPHRD_FCFABRIC + 2: return "fcfb2";
	case ARPHRD_FCFABRIC + 3: return "fcfb3";
	case ARPHRD_FCFABRIC + 4: return "fcfb4";
	case ARPHRD_FCFABRIC + 5: return "fcfb5";
	case ARPHRD_FCFABRIC + 6: return "fcfb6";
	case ARPHRD_FCFABRIC + 7: return "fcfb7";
	case ARPHRD_FCFABRIC + 8: return "fcfb8";
	case ARPHRD_FCFABRIC + 9: return "fcfb9";
	case ARPHRD_FCFABRIC + 10: return "fcfb10";
	case ARPHRD_FCFABRIC + 11: return "fcfb11";
	case ARPHRD_FCFABRIC + 12: return "fcfb12";
	case ARPHRD_IEEE802_TR: return "ieee802_tr";
	case ARPHRD_IEEE80211: return "ieee80211";
	case ARPHRD_IEEE80211_PRISM: return "ieee80211_prism";
	case ARPHRD_IEEE80211_RADIOTAP: return "ieee80211_radiotap";
	case ARPHRD_IEEE802154: return "ieee802154";
	case ARPHRD_PHONET: return "phonet";
	case ARPHRD_PHONET_PIPE: return "phonet_pipe";
	case ARPHRD_CAIF: return "caif";
	case ARPHRD_IP6GRE: return "ip6gre";
	case ARPHRD_NETLINK: return "netlink";
	case ARPHRD_NONE: return "none";
	case ARPHRD_VOID: return "void";

	default: return "Unknown";
	}
}

/* Taken from iproute2 */
static const char *ll_addr_n2a(const unsigned char *addr, int alen, int type,
		char *buf, int blen)
{
	int i;
	int l;

	if (alen == 4 &&
	    (type == ARPHRD_TUNNEL || type == ARPHRD_SIT || type == ARPHRD_IPGRE)) {
		return inet_ntop(AF_INET, addr, buf, blen);
	}
	if (alen == 16 && type == ARPHRD_TUNNEL6) {
		return inet_ntop(AF_INET6, addr, buf, blen);
	}
	l = 0;
	for (i=0; i<alen; i++) {
		if (i==0) {
			snprintf(buf+l, blen, "%02x", addr[i]);
			blen -= 2;
			l += 2;
		} else {
			snprintf(buf+l, blen, ":%02x", addr[i]);
			blen -= 3;
			l += 3;
		}
	}
	return buf;
}

static char *nlmsg_type2str(uint16_t proto, uint16_t type, char *buf, int len)
{
	if (proto == NETLINK_ROUTE && type < RTM_MAX) {
		const char *name = nlmsg_rtnl_type2str(type);
		if (name) {
			strncpy(buf, name, len);
			return buf;
		}
	}

	return nl_nlmsgtype2str(type, buf, len);
}

static const char *addr_family2str(uint16_t family)
{
	switch (family) {
	case AF_INET:	return "ipv4";
	case AF_INET6:	return "ipv6";
	case AF_DECnet:	return "decnet";
	case AF_IPX:	return "ipx";
	default:	return "Unknown";
	}
}

static const char *addr2str(uint16_t af, const void *addr, char *buf, int blen)
{
	if (af == AF_INET || af == AF_INET6)
		return inet_ntop(af, addr, buf, blen);

	return "???";
}

static void rtnl_print_ifinfo(struct nlmsghdr *hdr)
{
	struct ifinfomsg *ifi = NLMSG_DATA(hdr);
	struct rtattr *attr = IFLA_RTA(ifi);
	uint32_t attrs_len = IFLA_PAYLOAD(hdr);
	char flags[256];
	char if_addr[64] = {};
	char *af_link = "Unknown";

	if (ifi->ifi_family == AF_UNSPEC)
		af_link = "unspec";
	else if (ifi->ifi_family == AF_BRIDGE)
		af_link = "bridge";

	tprintf(" [ Link Family %d (%s%s%s)", ifi->ifi_family,
			colorize_start(bold), af_link, colorize_end());
	tprintf(", Type %d (%s%s%s)", ifi->ifi_type,
			colorize_start(bold),
			if_type2str(ifi->ifi_type),
			colorize_end());
	tprintf(", Index %d", ifi->ifi_index);
	tprintf(", Flags 0x%x (%s%s%s)", ifi->ifi_flags,
			colorize_start(bold),
			rtnl_link_flags2str(ifi->ifi_flags, flags,
				sizeof(flags)),
			colorize_end());
	tprintf(", Change 0x%x (%s%s%s) ]\n", ifi->ifi_change,
			colorize_start(bold),
			rtnl_link_flags2str(ifi->ifi_change, flags,
				sizeof(flags)),
			colorize_end());

	for (; RTA_OK(attr, attrs_len); attr = RTA_NEXT(attr, attrs_len)) {
		switch (attr->rta_type) {
		case IFLA_ADDRESS:
			attr_fmt(attr, "Address %s",
					ll_addr_n2a(RTA_DATA(attr),
						RTA_LEN(attr), ifi->ifi_type,
						if_addr, sizeof(if_addr)));
			break;
		case IFLA_BROADCAST:
			attr_fmt(attr, "Broadcast %s",
					ll_addr_n2a(RTA_DATA(attr),
						RTA_LEN(attr), ifi->ifi_type,
						if_addr, sizeof(if_addr)));
			break;
		case IFLA_IFNAME:
			attr_fmt(attr, "Name %s%s%s",
					colorize_start(bold), RTA_STR(attr),
					colorize_end());
			break;
		case IFLA_MTU:
			attr_fmt(attr, "MTU %d", RTA_INT(attr));
			break;
		case IFLA_LINK:
			attr_fmt(attr, "Link %d", RTA_INT(attr));
			break;
		case IFLA_QDISC:
			attr_fmt(attr, "QDisc %s", RTA_STR(attr));
			break;
		case IFLA_OPERSTATE:
			{
				uint8_t st = RTA_UINT8(attr);
				char states[256];

				attr_fmt(attr, "Operation state 0x%x (%s%s%s)",
						st,
						colorize_start(bold),
						rtnl_link_operstate2str(st,
							states, sizeof(states)),
						colorize_end());
			}
			break;
		case IFLA_LINKMODE:
			{
				uint8_t mode = RTA_UINT8(attr);
				char str[32];

				attr_fmt(attr, "Mode 0x%x (%s%s%s)", mode,
						colorize_start(bold),
						rtnl_link_mode2str(mode, str,
							sizeof(str)),
						colorize_end());
			}
			break;
		case IFLA_GROUP:
			attr_fmt(attr, "Group %d", RTA_INT(attr));
			break;
		case IFLA_TXQLEN:
			attr_fmt(attr, "Tx queue len %d", RTA_INT(attr));
			break;
		case IFLA_NET_NS_PID:
			attr_fmt(attr, "Network namespace pid %d",
					RTA_INT(attr));
			break;
		case IFLA_NET_NS_FD:
			attr_fmt(attr, "Network namespace fd %d",
					RTA_INT(attr));
			break;
		}
	}
}

static void rtnl_print_ifaddr(struct nlmsghdr *hdr)
{
	struct ifaddrmsg *ifa = NLMSG_DATA(hdr);
	uint32_t attrs_len = IFA_PAYLOAD(hdr);
	struct rtattr *attr = IFA_RTA(ifa);
	struct ifa_cacheinfo *ci;
	char *scope = "Unknown";
	char addr_str[256];
	char flags[256];

	if (ifa->ifa_scope == RT_SCOPE_UNIVERSE)
		scope = "global";
	else if (ifa->ifa_scope == RT_SCOPE_LINK)
		scope = "link";
	else if (ifa->ifa_scope == RT_SCOPE_HOST)
		scope = "host";
	else if (ifa->ifa_scope == RT_SCOPE_NOWHERE)
		scope = "nowhere";

	tprintf(" [ Address Family %d (%s%s%s)", ifa->ifa_family,
			colorize_start(bold),
			addr_family2str(ifa->ifa_family),
			colorize_end());
	tprintf(", Prefix Len %d", ifa->ifa_prefixlen);
	tprintf(", Flags %d (%s%s%s)", ifa->ifa_flags,
			colorize_start(bold),
			rtnl_addr_flags2str(ifa->ifa_flags, flags,
				sizeof(flags)),
			colorize_end());
	tprintf(", Scope %d (%s%s%s)", ifa->ifa_scope,
			colorize_start(bold), scope, colorize_end());
	tprintf(", Link Index %d ]\n", ifa->ifa_index);

	for (; RTA_OK(attr, attrs_len); attr = RTA_NEXT(attr, attrs_len)) {
		switch (attr->rta_type) {
		case IFA_LOCAL:
			attr_fmt(attr, "Local %s", addr2str(ifa->ifa_family,
				RTA_DATA(attr), addr_str, sizeof(addr_str)));
			break;
		case IFA_ADDRESS:
			attr_fmt(attr, "Address %s", addr2str(ifa->ifa_family,
				RTA_DATA(attr), addr_str, sizeof(addr_str)));
			break;
		case IFA_BROADCAST:
			attr_fmt(attr, "Broadcast %s",
					addr2str(ifa->ifa_family,
						RTA_DATA(attr), addr_str,
						sizeof(addr_str)));
			break;
		case IFA_MULTICAST:
			attr_fmt(attr, "Multicast %s",
					addr2str(ifa->ifa_family,
						RTA_DATA(attr), addr_str,
						sizeof(addr_str)));
			break;
		case IFA_ANYCAST:
			attr_fmt(attr, "Anycast %s", addr2str(ifa->ifa_family,
				RTA_DATA(attr), addr_str, sizeof(addr_str)));
			break;
		case IFA_FLAGS:
			attr_fmt(attr, "Flags %d (%s%s%s)", RTA_INT(attr),
				colorize_start(bold),
				rtnl_addr_flags2str(RTA_INT(attr),
					flags, sizeof(flags)),
				colorize_end());
			break;
		case IFA_LABEL:
			attr_fmt(attr, "Label %s", RTA_STR(attr));
			break;
		case IFA_CACHEINFO:
			ci = RTA_DATA(attr);
			tprintf("\tA: Cache (");

			if (ci->ifa_valid == INFINITY)
				tprintf("valid lft(forever)");
			else
				tprintf("valid lft(%us)", ci->ifa_valid);

			if (ci->ifa_prefered == INFINITY)
				tprintf(", prefrd lft(forever)");
			else
				tprintf(", prefrd lft(%us)", ci->ifa_prefered);

			tprintf(", created on(%.2fs)", (double)ci->cstamp / 100);
			tprintf(", updated on(%.2fs))", (double)ci->cstamp / 100);
			tprintf(", Len %u\n", RTA_LEN(attr));
			break;
		}
	}
}

static void rtnl_msg_print(struct nlmsghdr *hdr)
{
	switch (hdr->nlmsg_type) {
	case RTM_NEWLINK:
	case RTM_DELLINK:
	case RTM_GETLINK:
	case RTM_SETLINK:
		rtnl_print_ifinfo(hdr);
		break;
	case RTM_NEWADDR:
	case RTM_DELADDR:
	case RTM_GETADDR:
		rtnl_print_ifaddr(hdr);
		break;
	}
}

static void nlmsg_print(uint16_t family, struct nlmsghdr *hdr)
{
	char type[32];
	char flags[128];
	char procname[PATH_MAX];

	/* Look up the process name if message is not coming from the kernel.
	 *
	 * Note that the port id is not necessarily equal to the PID of the
	 * receiving process (e.g. if the application is multithreaded or using
	 * multiple sockets). In these cases we're not able to find a matching
	 * PID and the information will not be printed.
	 */
	if (hdr->nlmsg_pid != 0) {
		char path[1024];
		int ret;

		snprintf(path, sizeof(path), "/proc/%u/exe", hdr->nlmsg_pid);
		ret = readlink(path, procname, sizeof(procname) - 1);
		if (ret < 0)
			ret = 0;
		procname[ret] = '\0';
	} else
		snprintf(procname, sizeof(procname), "kernel");

	tprintf(" [ NLMSG ");
	tprintf("Family %d (%s%s%s), ", family,
		colorize_start(bold),
		nlmsg_family2str(family),
		colorize_end());
	tprintf("Len %u, ", hdr->nlmsg_len);
	tprintf("Type 0x%.4x (%s%s%s), ", hdr->nlmsg_type,
		colorize_start(bold),
		nlmsg_type2str(family, hdr->nlmsg_type, type, sizeof(type)),
		colorize_end());
	tprintf("Flags 0x%.4x (%s%s%s), ", hdr->nlmsg_flags,
		colorize_start(bold),
		nl_nlmsg_flags2str(hdr->nlmsg_flags, flags, sizeof(flags)),
		colorize_end());
	tprintf("Seq-Nr %u, ", hdr->nlmsg_seq);
	tprintf("PID %u", hdr->nlmsg_pid);
	if (procname[0])
		tprintf(" (%s%s%s)", colorize_start(bold), basename(procname),
			colorize_end());
	tprintf(" ]\n");

	if (family == NETLINK_ROUTE)
		rtnl_msg_print(hdr);
}

static void nlmsg(struct pkt_buff *pkt)
{
	struct nlmsghdr *hdr = (struct nlmsghdr *) pkt_pull(pkt, sizeof(*hdr));

	while (hdr) {
		nlmsg_print(ntohs(pkt->proto), hdr);

		if (!pkt_pull(pkt, NLMSG_PAYLOAD(hdr, 0)))
			break;

		hdr = (struct nlmsghdr *) pkt_pull(pkt, sizeof(*hdr));
		if (hdr && hdr->nlmsg_type != NLMSG_DONE &&
				(hdr->nlmsg_flags & NLM_F_MULTI))
			tprintf("\n");
	}
}

static void nlmsg_less(struct pkt_buff *pkt)
{
	struct nlmsghdr *hdr = (struct nlmsghdr *) pkt_pull(pkt, sizeof(*hdr));
	uint16_t family = ntohs(pkt->proto);
	char type[32];

	if (hdr == NULL)
		return;

	tprintf(" NLMSG Family %d (%s%s%s), ", family,
		colorize_start(bold),
		nlmsg_family2str(family),
		colorize_end());
	tprintf("Type %u (%s%s%s)", hdr->nlmsg_type,
		colorize_start(bold),
		nlmsg_type2str(family, hdr->nlmsg_type, type, sizeof(type)),
		colorize_end());
}

struct protocol nlmsg_ops = {
	.print_full = nlmsg,
	.print_less = nlmsg_less,
};
