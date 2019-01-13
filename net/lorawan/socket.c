// SPDX-License-Identifier: GPL-2.0-or-later OR BSD-3-Clause
/*
 * LoRaWAN stack related definitions
 *
 * Copyright (c) 2018 Jian-Hong Pan <starnight@g.ncu.edu.tw>
 */

#define	LORAWAN_MODULE_NAME	"lorawan"

#define	pr_fmt(fmt)		LORAWAN_MODULE_NAME ": " fmt

#include <linux/if_arp.h>
#include <linux/init.h>
#include <linux/list.h>
#include <linux/lora/lorawan_netdev.h>
#include <linux/module.h>
#include <linux/net.h>
#include <linux/termios.h>		/* For TIOCOUTQ/INQ */
#include <net/sock.h>

/**
 * dgram_sock - This structure holds the states of Datagram socket
 *
 * @sk:			network layer representation of the socket
 * @src_devaddr:	the LoRaWAN device address for this connection
 * @bound:		this socket is bound or not
 * @connected:		this socket is connected to the destination or not
 */
struct dgram_sock {
	struct sock sk;	/* sk must be the first member of dgram_sock */
	u32 src_devaddr;

	u8 bound:1;
	u8 connected:1;
};

static HLIST_HEAD(dgram_head);
static DEFINE_RWLOCK(dgram_lock);

static struct dgram_sock *
dgram_sk(const struct sock *sk)
{
	return container_of(sk, struct dgram_sock, sk);
}

static struct net_device *
lrw_get_dev_by_addr(struct net *net, u32 devaddr)
{
	__be32 be_addr = cpu_to_be32(devaddr);
	struct net_device *ndev = NULL;

	rcu_read_lock();
	ndev = dev_getbyhwaddr_rcu(net, ARPHRD_LORAWAN, (char *)&be_addr);
	if (ndev && ndev->type == ARPHRD_LORAWAN)
		dev_hold(ndev);
	else
		ndev = NULL;
	rcu_read_unlock();

	return ndev;
}

static int
dgram_init(struct sock *sk)
{
	return 0;
}

static void
dgram_close(struct sock *sk, long timeout)
{
	sk_common_release(sk);
}

static int
dgram_bind(struct sock *sk, struct sockaddr *uaddr, int len)
{
	struct sockaddr_lorawan *addr = (struct sockaddr_lorawan *)uaddr;
	struct dgram_sock *ro = dgram_sk(sk);
	struct net_device *ndev;
	int ret;

	lock_sock(sk);
	ro->bound = 0;

	ret = -EINVAL;
	if (len < sizeof(*addr))
		goto dgram_bind_end;

	if (addr->family != AF_LORAWAN)
		goto dgram_bind_end;

	if (addr->addr_in.addr_type != LRW_ADDR_DEVADDR)
		goto dgram_bind_end;

	pr_debug("%s: bind address %X\n", __func__, addr->addr_in.devaddr);
	ndev = lrw_get_dev_by_addr(sock_net(sk), addr->addr_in.devaddr);
	if (!ndev) {
		ret = -ENODEV;
		goto dgram_bind_end;
	}
	netdev_dbg(ndev, "%s: get ndev\n", __func__);

	ro->src_devaddr = addr->addr_in.devaddr;
	ro->bound = 1;
	ret = 0;
	dev_put(ndev);
	pr_debug("%s: bound address %X\n", __func__, ro->src_devaddr);

dgram_bind_end:
	release_sock(sk);
	return ret;
}

static int
dgram_sendmsg(struct sock *sk, struct msghdr *msg, size_t size)
{
	struct dgram_sock *ro = dgram_sk(sk);
	struct net_device *ndev;
	struct sk_buff *skb;
	size_t hlen;
	size_t tlen;
	int ret;

	pr_debug("%s: going to send %zu bytes\n", __func__, size);
	if (msg->msg_flags & MSG_OOB) {
		pr_debug("msg->msg_flags = 0x%x\n", msg->msg_flags);
		return -EOPNOTSUPP;
	}

	pr_debug("%s: check msg_name\n", __func__);
	if (!ro->connected && !msg->msg_name)
		return -EDESTADDRREQ;
	else if (ro->connected && msg->msg_name)
		return -EISCONN;

	pr_debug("%s: check bound\n", __func__);
	if (!ro->bound)
		ndev = dev_getfirstbyhwtype(sock_net(sk), ARPHRD_LORAWAN);
	else
		ndev = lrw_get_dev_by_addr(sock_net(sk), ro->src_devaddr);

	if (!ndev) {
		pr_debug("no dev\n");
		ret = -ENXIO;
		goto dgram_sendmsg_end;
	}

	if (size > ndev->mtu) {
		netdev_dbg(ndev, "size = %zu, mtu = %u\n", size, ndev->mtu);
		ret = -EMSGSIZE;
		goto dgram_sendmsg_no_skb;
	}

	netdev_dbg(ndev, "%s: create skb\n", __func__);
	hlen = LL_RESERVED_SPACE(ndev);
	tlen = ndev->needed_tailroom;
	skb = sock_alloc_send_skb(sk, hlen + tlen + size,
				  msg->msg_flags & MSG_DONTWAIT,
				  &ret);

	if (!skb)
		goto dgram_sendmsg_no_skb;

	skb_reserve(skb, hlen);
	skb_reset_network_header(skb);

	ret = memcpy_from_msg(skb_put(skb, size), msg, size);
	if (ret > 0)
		goto dgram_sendmsg_err_skb;

	skb->dev = ndev;
	skb->protocol = htons(ETH_P_LORAWAN);

	netdev_dbg(ndev, "%s: push skb to xmit queue\n", __func__);
	ret = dev_queue_xmit(skb);
	if (ret > 0)
		ret = net_xmit_errno(ret);
	netdev_dbg(ndev, "%s: pushed skb to xmit queue with ret=%d\n",
		   __func__, ret);
	dev_put(ndev);

	return ret ?: size;

dgram_sendmsg_err_skb:
	kfree_skb(skb);
dgram_sendmsg_no_skb:
	dev_put(ndev);
dgram_sendmsg_end:
	return ret;
}

static int
dgram_recvmsg(struct sock *sk, struct msghdr *msg, size_t len,
	      int noblock, int flags, int *addr_len)
{
	DECLARE_SOCKADDR(struct sockaddr_lorawan *, saddr, msg->msg_name);
	struct sk_buff *skb;
	size_t copied = 0;
	int err;

	skb = skb_recv_datagram(sk, flags, noblock, &err);
	if (!skb)
		goto dgram_recvmsg_end;

	copied = skb->len;
	if (len < copied) {
		msg->msg_flags |= MSG_TRUNC;
		copied = len;
	}

	err = skb_copy_datagram_msg(skb, 0, msg, copied);
	if (err)
		goto dgram_recvmsg_done;

	sock_recv_ts_and_drops(msg, sk, skb);
	if (saddr) {
		memset(saddr, 0, sizeof(*saddr));
		saddr->family = AF_LORAWAN;
		saddr->addr_in.devaddr = lrw_get_mac_cb(skb)->devaddr;
		*addr_len = sizeof(*saddr);
	}

	if (flags & MSG_TRUNC)
		copied = skb->len;

dgram_recvmsg_done:
	skb_free_datagram(sk, skb);

dgram_recvmsg_end:
	if (err)
		return err;
	return copied;
}

static int
dgram_hash(struct sock *sk)
{
	pr_debug("%s\n", __func__);
	write_lock_bh(&dgram_lock);
	sk_add_node(sk, &dgram_head);
	sock_prot_inuse_add(sock_net(sk), sk->sk_prot, 1);
	write_unlock_bh(&dgram_lock);

	return 0;
}

static void
dgram_unhash(struct sock *sk)
{
	pr_debug("%s\n", __func__);
	write_lock_bh(&dgram_lock);
	if (sk_del_node_init(sk))
		sock_prot_inuse_add(sock_net(sk), sk->sk_prot, -1);
	write_unlock_bh(&dgram_lock);
}

static int
dgram_connect(struct sock *sk, struct sockaddr *uaddr, int len)
{
	struct dgram_sock *ro = dgram_sk(sk);

	/* Nodes of LoRaWAN send data to a gateway only, then data is received
	 * and transferred to servers with the gateway's policy.
	 * So, the destination address is not used by nodes.
	 */
	lock_sock(sk);
	ro->connected = 1;
	release_sock(sk);

	return 0;
}

static int
dgram_disconnect(struct sock *sk, int flags)
{
	struct dgram_sock *ro = dgram_sk(sk);

	lock_sock(sk);
	ro->connected = 0;
	release_sock(sk);

	return 0;
}

static int
dgram_ioctl(struct sock *sk, int cmd, unsigned long arg)
{
	struct net_device *ndev = sk->sk_dst_cache->dev;
	struct sk_buff *skb;
	int amount;
	int err;

	netdev_dbg(ndev, "%s: ioctl file (cmd=0x%X)\n", __func__, cmd);
	switch (cmd) {
	case SIOCOUTQ:
		amount = sk_wmem_alloc_get(sk);
		err = put_user(amount, (int __user *)arg);
		break;
	case SIOCINQ:
		amount = 0;
		spin_lock_bh(&sk->sk_receive_queue.lock);
		skb = skb_peek(&sk->sk_receive_queue);
		if (skb) {
			/* We will only return the amount of this packet
			 * since that is all that will be read.
			 */
			amount = skb->len;
		}
		spin_unlock_bh(&sk->sk_receive_queue.lock);
		err = put_user(amount, (int __user *)arg);
		break;
	default:
		err = -ENOIOCTLCMD;
	}

	return err;
}

static int
dgram_getsockopt(struct sock *sk, int level, int optname,
		 char __user *optval, int __user *optlen)
{
	int val, len;

	if (level != SOL_LORAWAN)
		return -EOPNOTSUPP;

	if (get_user(len, optlen))
		return -EFAULT;

	len = min_t(unsigned int, len, sizeof(int));

	switch (optname) {
	default:
		return -ENOPROTOOPT;
	}

	if (put_user(len, optlen))
		return -EFAULT;

	if (copy_to_user(optval, &val, len))
		return -EFAULT;

	return 0;
}

static int
dgram_setsockopt(struct sock *sk, int level, int optname,
		 char __user *optval, unsigned int optlen)
{
	int val;
	int err;

	err = 0;

	if (optlen < sizeof(int))
		return -EINVAL;

	if (get_user(val, (int __user *)optval))
		return -EFAULT;

	lock_sock(sk);

	switch (optname) {
	default:
		err = -ENOPROTOOPT;
		break;
	}

	release_sock(sk);

	return err;
}

static struct proto lrw_dgram_prot = {
	.name		= "LoRaWAN",
	.owner		= THIS_MODULE,
	.obj_size	= sizeof(struct dgram_sock),
	.init		= dgram_init,
	.close		= dgram_close,
	.bind		= dgram_bind,
	.sendmsg	= dgram_sendmsg,
	.recvmsg	= dgram_recvmsg,
	.hash		= dgram_hash,
	.unhash		= dgram_unhash,
	.connect	= dgram_connect,
	.disconnect	= dgram_disconnect,
	.ioctl		= dgram_ioctl,
	.getsockopt	= dgram_getsockopt,
	.setsockopt	= dgram_setsockopt,
};

static int
lrw_sock_release(struct socket *sock)
{
	struct sock *sk = sock->sk;

	if (sk) {
		sock->sk = NULL;
		sk->sk_prot->close(sk, 0);
	}

	return 0;
}

static int
lrw_sock_bind(struct socket *sock, struct sockaddr *uaddr, int addr_len)
{
	struct sockaddr_lorawan *addr = (struct sockaddr_lorawan *)uaddr;
	struct sock *sk = sock->sk;

	pr_debug("%s: bind address %X\n", __func__, addr->addr_in.devaddr);
	if (sk->sk_prot->bind)
		return sk->sk_prot->bind(sk, uaddr, addr_len);

	return sock_no_bind(sock, uaddr, addr_len);
}

static int
lrw_sock_connect(struct socket *sock, struct sockaddr *uaddr,
		 int addr_len, int flags)
{
	struct sock *sk = sock->sk;

	if (addr_len < sizeof(uaddr->sa_family))
		return -EINVAL;

	return sk->sk_prot->connect(sk, uaddr, addr_len);
}

static int
lrw_ndev_ioctl(struct sock *sk, struct ifreq __user *arg, unsigned int cmd)
{
	struct net_device *ndev;
	struct ifreq ifr;
	int ret;

	pr_debug("%s: cmd %ud\n", __func__, cmd);
	ret = -ENOIOCTLCMD;

	if (copy_from_user(&ifr, arg, sizeof(struct ifreq)))
		return -EFAULT;

	ifr.ifr_name[IFNAMSIZ - 1] = 0;

	dev_load(sock_net(sk), ifr.ifr_name);
	ndev = dev_get_by_name(sock_net(sk), ifr.ifr_name);

	netdev_dbg(ndev, "%s: cmd %ud\n", __func__, cmd);
	if (!ndev)
		return -ENODEV;

	if (ndev->type == ARPHRD_LORAWAN && ndev->netdev_ops->ndo_do_ioctl)
		ret = ndev->netdev_ops->ndo_do_ioctl(ndev, &ifr, cmd);

	if (!ret && copy_to_user(arg, &ifr, sizeof(struct ifreq)))
		ret = -EFAULT;
	dev_put(ndev);

	return ret;
}

static int
lrw_sock_ioctl(struct socket *sock, unsigned int cmd, unsigned long arg)
{
	struct sock *sk = sock->sk;

	pr_debug("%s: cmd %ud\n", __func__, cmd);
	switch (cmd) {
	case SIOCGSTAMP:
		return sock_get_timestamp(sk, (struct timeval __user *)arg);
	case SIOCGSTAMPNS:
		return sock_get_timestampns(sk, (struct timespec __user *)arg);
	case SIOCOUTQ:
	case SIOCINQ:
		if (!sk->sk_prot->ioctl)
			return -ENOIOCTLCMD;
		return sk->sk_prot->ioctl(sk, cmd, arg);
	default:
		return lrw_ndev_ioctl(sk, (struct ifreq __user *)arg, cmd);
	}
}

static int
lrw_sock_sendmsg(struct socket *sock, struct msghdr *msg, size_t len)
{
	struct sock *sk = sock->sk;

	pr_debug("%s: going to send %zu bytes\n", __func__, len);
	return sk->sk_prot->sendmsg(sk, msg, len);
}

static const struct proto_ops lrw_dgram_ops = {
	.family		= PF_LORAWAN,
	.owner		= THIS_MODULE,
	.release	= lrw_sock_release,
	.bind		= lrw_sock_bind,
	.connect	= lrw_sock_connect,
	.socketpair	= sock_no_socketpair,
	.accept		= sock_no_accept,
	.getname	= sock_no_getname,
	.poll		= datagram_poll,
	.ioctl		= lrw_sock_ioctl,
	.listen		= sock_no_listen,
	.shutdown	= sock_no_shutdown,
	.setsockopt	= sock_common_setsockopt,
	.getsockopt	= sock_common_getsockopt,
	.sendmsg	= lrw_sock_sendmsg,
	.recvmsg	= sock_common_recvmsg,
	.mmap		= sock_no_mmap,
	.sendpage	= sock_no_sendpage,
};

static int
lrw_create(struct net *net, struct socket *sock, int protocol, int kern)
{
	struct sock *sk;
	int ret;

	if (!net_eq(net, &init_net))
		return -EAFNOSUPPORT;

	if (sock->type != SOCK_DGRAM)
		return -EAFNOSUPPORT;

	/* Allocates enough memory for dgram_sock whose first member is sk */
	sk = sk_alloc(net, PF_LORAWAN, GFP_KERNEL, &lrw_dgram_prot, kern);
	if (!sk)
		return -ENOMEM;

	sock->ops = &lrw_dgram_ops;
	sock_init_data(sock, sk);
	sk->sk_family = PF_LORAWAN;
	sock_set_flag(sk, SOCK_ZAPPED);

	if (sk->sk_prot->hash) {
		ret = sk->sk_prot->hash(sk);
		if (ret) {
			sk_common_release(sk);
			goto lrw_create_end;
		}
	}

	if (sk->sk_prot->init) {
		ret = sk->sk_prot->init(sk);
		if (ret)
			sk_common_release(sk);
	}

lrw_create_end:
	return ret;
}

static const struct net_proto_family lorawan_family_ops = {
	.owner		= THIS_MODULE,
	.family		= PF_LORAWAN,
	.create		= lrw_create,
};

static int
lrw_dgram_deliver(struct net_device *ndev, struct sk_buff *skb)
{
	struct dgram_sock *ro;
	struct sock *sk;
	bool found;
	int ret;

	ret = NET_RX_SUCCESS;
	found = false;

	read_lock(&dgram_lock);
	sk_for_each(sk, &dgram_head) {
		ro = dgram_sk(sk);
		if (cpu_to_be32(ro->src_devaddr) == *(__be32 *)ndev->dev_addr) {
			found = true;
			break;
		}
	}
	read_unlock(&dgram_lock);

	if (!found)
		goto lrw_dgram_deliver_err;

	skb = skb_share_check(skb, GFP_ATOMIC);
	if (!skb)
		return NET_RX_DROP;

	if (sock_queue_rcv_skb(sk, skb) < 0)
		goto lrw_dgram_deliver_err;

	return ret;

lrw_dgram_deliver_err:
	kfree_skb(skb);
	ret = NET_RX_DROP;
	return ret;
}

static int
lrw_rcv(struct sk_buff *skb, struct net_device *ndev,
	struct packet_type *pt, struct net_device *orig_ndev)
{
	if (!netif_running(ndev))
		goto lrw_rcv_drop;

	if (!net_eq(dev_net(ndev), &init_net))
		goto lrw_rcv_drop;

	if (ndev->type != ARPHRD_LORAWAN)
		goto lrw_rcv_drop;

	if (skb->pkt_type != PACKET_OTHERHOST)
		return lrw_dgram_deliver(ndev, skb);

lrw_rcv_drop:
	kfree_skb(skb);
	return NET_RX_DROP;
}

static struct packet_type lorawan_packet_type = {
	.type		= htons(ETH_P_LORAWAN),
	.func		= lrw_rcv,
};

static int __init
lrw_sock_init(void)
{
	int ret;

	pr_info("module inserted\n");
	ret = proto_register(&lrw_dgram_prot, 1);
	if (ret)
		goto lrw_sock_init_end;

	/* Tell SOCKET that we are alive */
	ret = sock_register(&lorawan_family_ops);
	if (ret)
		goto lrw_sock_init_err;

	dev_add_pack(&lorawan_packet_type);
	ret = 0;
	goto lrw_sock_init_end;

lrw_sock_init_err:
	proto_unregister(&lrw_dgram_prot);

lrw_sock_init_end:
	return ret;
}

static void __exit
lrw_sock_exit(void)
{
	dev_remove_pack(&lorawan_packet_type);
	sock_unregister(PF_LORAWAN);
	proto_unregister(&lrw_dgram_prot);
	pr_info("module removed\n");
}

module_init(lrw_sock_init);
module_exit(lrw_sock_exit);

MODULE_AUTHOR("Jian-Hong Pan <starnight@g.ncu.edu.tw>");
MODULE_DESCRIPTION("LoRaWAN socket protocol");
MODULE_LICENSE("Dual BSD/GPL");
MODULE_ALIAS_NETPROTO(PF_LORAWAN);
