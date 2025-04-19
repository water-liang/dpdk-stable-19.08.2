

#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_mbuf.h>

#include <stdio.h>
#include <arpa/inet.h>

#define NUM_MBUFS (4096-1)

#define BURST_SIZE	32

int gDpdkPortId = 0;

// ethernet 端口配置
// 如果数据包长度超过 max_rx_pkt_len，
// 网卡会直接丢弃该数据包，并可能增加硬件统计计数器（如丢弃包计数器
static const struct rte_eth_conf port_conf_default = {
	.rxmode = {.max_rx_pkt_len = RTE_ETHER_MAX_LEN }
};

static void ng_init_port(struct rte_mempool *mbuf_pool) {

    // 获取dpdk网卡设备数量
	uint16_t nb_sys_ports= rte_eth_dev_count_avail(); //
	if (nb_sys_ports == 0) {
		rte_exit(EXIT_FAILURE, "No Supported eth found\n");
	}

    // 获取第一个网卡设备的信息
	struct rte_eth_dev_info dev_info;
	rte_eth_dev_info_get(gDpdkPortId, &dev_info); //
	
    // 配置使用的队列
	const int num_rx_queues = 1;
	const int num_tx_queues = 0;
	struct rte_eth_conf port_conf = port_conf_default;
	rte_eth_dev_configure(gDpdkPortId, num_rx_queues, num_tx_queues, &port_conf);

    // 启动rx队列
    // 这里的0表示使用第一个队列，128表示每个队列的最多接受的包的数量
    /*
    第一个参数是网卡设备的 ID（gDpdkPortId）。
    第二个参数是队列索引，这里为 0，表示第一个接收队列。
    第三个参数是队列的大小（128）。
    第四个参数是网卡设备所在的 NUMA 节点 ID，使用 rte_eth_dev_socket_id 获取。
    第五个参数是队列的配置结构体，这里传入 NULL 表示使用默认配置。
    第六个参数是内存池（mbuf_pool），用于存储接收到的数据包
    */
	if (rte_eth_rx_queue_setup(gDpdkPortId, 0 , 128, 
		rte_eth_dev_socket_id(gDpdkPortId),NULL, mbuf_pool) < 0) {

		rte_exit(EXIT_FAILURE, "Could not setup RX queue\n");

	}

    // 启动网卡设备
	if (rte_eth_dev_start(gDpdkPortId) < 0 ) {
		rte_exit(EXIT_FAILURE, "Could not start\n");
	}

    rte_eth_promiscuous_enable(gDpdkPortId); // 设置网卡为混杂模式
	
}


int main(int argc, char *argv[]) {

    // 环境初始化
	if (rte_eal_init(argc, argv) < 0) {
		rte_exit(EXIT_FAILURE, "Error with EAL init\n");
		
	}

    // 内存池
	struct rte_mempool *mbuf_pool = rte_pktmbuf_pool_create("mbuf pool", NUM_MBUFS,
		0, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	if (mbuf_pool == NULL) {
		rte_exit(EXIT_FAILURE, "Could not create mbuf pool\n");
	}

    //网卡初始化
	ng_init_port(mbuf_pool);

    // 收包处理
	while (1) {

		struct rte_mbuf *mbufs[BURST_SIZE];
		unsigned num_recvd = rte_eth_rx_burst(gDpdkPortId, 0, mbufs, BURST_SIZE);
		if (num_recvd > BURST_SIZE) {
			rte_exit(EXIT_FAILURE, "Error receiving from eth\n");
		}

		unsigned i = 0;
		for (i = 0;i < num_recvd;i ++) {

            //指针转换
            // 获取ethernet头部
			struct rte_ether_hdr *ehdr = rte_pktmbuf_mtod(mbufs[i], struct rte_ether_hdr*);
			if (ehdr->ether_type != rte_cpu_to_be_16(RTE_ETHER_TYPE_IPV4)) {
                rte_pktmbuf_free(mbufs[i]);
				continue;
			}

            // 获取ipv4头部
			struct rte_ipv4_hdr *iphdr =  rte_pktmbuf_mtod_offset(mbufs[i], struct rte_ipv4_hdr *, 
				sizeof(struct rte_ether_hdr));
			
			if (iphdr->next_proto_id == IPPROTO_UDP) {

				struct rte_udp_hdr *udphdr = (struct rte_udp_hdr *)(iphdr + 1);

				uint16_t length = ntohs(udphdr->dgram_len);
                // 引用这个位置的内存地址，并将其设置为 '\0'（空字符）
                // 使得数据报的内容可以被视为一个以 '\0' 结尾的C字符串
				*((char*)udphdr + length) = '\0'; 

				struct in_addr addr;
				addr.s_addr = iphdr->src_addr;
				printf("src: %s:%d, ", inet_ntoa(addr),ntohs( udphdr->src_port));

				addr.s_addr = iphdr->dst_addr;
				printf("dst: %s:%d, %s\n", inet_ntoa(addr), ntohs(udphdr->src_port), 
					(char *)(udphdr+1));
                
                // rte_pktmbuf_free 的调用会将 mbuf 归还到内存池
				rte_pktmbuf_free(mbufs[i]);
			}
			
		}

	}

}




