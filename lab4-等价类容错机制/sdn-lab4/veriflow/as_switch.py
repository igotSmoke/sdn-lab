from ryu.base import app_manager
from ryu.base.app_manager import lookup_service_brick
from ryu.controller import ofp_event
from ryu.controller.handler import MAIN_DISPATCHER, CONFIG_DISPATCHER
from ryu.controller.handler import set_ev_cls
from ryu.ofproto import ofproto_v1_0
from ryu.lib.packet import packet
from ryu.lib.packet import ethernet, arp, ipv4
import utils.flowmod
import utils.ipv4
from utils.flowmod import send_flow_mod
from network_awareness import NetworkAwareness
import json
import os
import math

# 路由交换机类，继承自RyuApp
class RoutingSwitch(app_manager.RyuApp):
    # 指定OpenFlow版本为1.0
    OFP_VERSIONS = [ofproto_v1_0.OFP_VERSION]

    # 定义应用上下文，包含网络感知模块
    _CONTEXTS = {
        'network_awareness': NetworkAwareness
    }

    def __init__(self, *args, **kwargs):
        # 初始化父类
        super(RoutingSwitch, self).__init__(*args, **kwargs)
        # 获取网络感知模块实例
        self.network_awareness = kwargs['network_awareness']
        # 初始化MAC地址到端口的映射表
        self.dpid_mac_port = {}
        # 从环境变量读取配置文件
        with open(os.environ["CONFIG"], "r") as f:
            self.routing_cfg = json.load(f)

    # 处理数据包进入事件
    @set_ev_cls(ofp_event.EventOFPPacketIn, MAIN_DISPATCHER)
    def packet_in_handler(self, ev):
        msg = ev.msg
        dp = msg.datapath
        ofp = dp.ofproto
        parser = dp.ofproto_parser

        dpid = dp.id
        in_port = msg.in_port

        # 解析数据包
        pkt = packet.Packet(msg.data)
        eth_pkt = pkt.get_protocol(ethernet.ethernet)
        arp_pkt = pkt.get_protocol(arp.arp)
        ipv4_pkt = pkt.get_protocol(ipv4.ipv4)

        pkt_type = eth_pkt.ethertype

        # 获取源MAC和目的MAC
        dst_mac = eth_pkt.dst
        src_mac = eth_pkt.src

        # 更新MAC地址到端口的映射
        self.dpid_mac_port.setdefault(dpid, {})
        self.dpid_mac_port[dpid][src_mac] = in_port

        # 根据数据包类型分别处理
        if isinstance(arp_pkt, arp.arp):
            self.handle_arp(msg, in_port, dst_mac, pkt_type)

        if isinstance(ipv4_pkt, ipv4.ipv4):
            self.handle_ipv4(msg, dpid, in_port, src_mac, dst_mac, ipv4_pkt.src, ipv4_pkt.dst, pkt_type)

    # 处理ARP数据包
    def handle_arp(self, msg, in_port, dst_mac, pkt_type):
        dp = msg.datapath
        ofp = dp.ofproto
        parser = dp.ofproto_parser

        dpid = dp.id

        # 如果知道目的MAC对应的端口，直接转发
        if dst_mac in self.dpid_mac_port[dpid]:
            out_port = self.dpid_mac_port[dpid][dst_mac]
            actions = [parser.OFPActionOutput(out_port)]
            out = parser.OFPPacketOut(
                datapath=dp, buffer_id=msg.buffer_id, in_port=in_port, actions=actions, data=msg.data)
            dp.send_msg(out)
        else:
            # 如果不知道目的MAC，则泛洪到所有主机端口
            for d, ports in self.network_awareness.port_info.items():
                for p in ports:
                    # 排除源端口
                    if d == dpid and p == in_port:
                        continue
                    dp = self.network_awareness.switch_info[d]
                    actions = [parser.OFPActionOutput(p)]
                    out = parser.OFPPacketOut(
                        datapath=dp, buffer_id=msg.buffer_id, in_port=ofp.OFPP_CONTROLLER, actions=actions, data=msg.data)
                    dp.send_msg(out)

    # 处理IPv4数据包
    def handle_ipv4(self, msg, dpid, in_port, src_mac, dst_mac, src_ip, dst_ip, pkt_type):
        print(f"Packet to {dpid}")
        parser = msg.datapath.ofproto_parser

        # 获取交换机所属的网络
        switch_net = self.routing_cfg["switch_nets"][dpid]
        srcnet = None
        dstnet = None
        
        # 确定源IP所属的网络
        for net in self.routing_cfg["gateways"]:
            if utils.ipv4.in_net(net, src_ip):
                srcnet = net
                break
    
        # 判断目的IP是否在同一AS内
        if utils.ipv4.in_net(switch_net, dst_ip):
            # 如果是同一AS内，直接使用最短路径
            dstnet = switch_net
            gateways = None
        else:
            # 如果是跨AS，需要找到合适的网关
            for dst_candidate in self.routing_cfg["gateways"][switch_net]:
                if utils.ipv4.in_net(dst_candidate, dst_ip):
                    dstnet = dst_candidate
                    gateways = self.routing_cfg["gateways"][switch_net][dstnet]
            
        if not srcnet or not dstnet:
            print("src / dst not recognized, unable to forward.")
            return

        # 添加路径函数，用于安装流表规则
        def add_path(route, dl_src, dl_dst, nw_src, nw_dst, priority=5):
            port_path = []
            # 计算路径上的所有端口
            for i in range(len(route) - 1):
                out_port = self.network_awareness.link_info[(route[i], route[i + 1])]
                port_path.append((route[i], out_port))
            self.show_path(route[0], route[-1], port_path)
            
            # 为路径上的每个交换机安装流表规则
            for node in port_path:
                waypoint_dpid, out_port = node
                send_flow_mod(waypoint_dpid, dl_src, dl_dst, nw_src, nw_dst, None, out_port, priority)
            
            return port_path[0][1]

        if gateways is None:
            # 处理AS内部的数据包转发
            dpid_path = self.network_awareness.shortest_path(dpid, dst_ip)
            if not dpid_path:
                return
            out_port = add_path(dpid_path, None, None, src_ip, dst_ip)
        else:
            # 处理跨AS的数据包转发
            if dpid in gateways:
                # 如果当前交换机是网关，直接发送给对等网关
                peer = self.routing_cfg["peers"][str(dpid)][dstnet]
                route = [dpid, peer]
                out_port = add_path(route, None, None, srcnet, dstnet)
                
            else:
                # 如果不是网关，发送给最近的网关
                min_delay = math.inf
                min_gw = None
                # 找到延迟最小的网关
                for gw in gateways:
                    delay = self.network_awareness.shortest_path_length(dpid, gw)
                    if delay < 0:
                        continue

                    if delay < min_delay:
                        min_delay = delay
                        min_gw = gw
                
                if min_gw is None:
                    return
                
                # 计算到最近网关的最短路径
                dpid_path = self.network_awareness.shortest_path(dpid, min_gw)
                if not dpid_path:
                    return
                
                out_port = add_path(dpid_path, None, None, srcnet, dstnet)
        
        # 发送数据包
        dp = self.network_awareness.switch_info[dpid]
        actions = [parser.OFPActionOutput(out_port)]
        out = parser.OFPPacketOut(
            datapath=dp, buffer_id=msg.buffer_id, in_port=in_port, actions=actions, data=msg.data)
        dp.send_msg(out)
            
    # 显示路径信息
    def show_path(self, src, dst, port_path):
        self.logger.info('path: {} -> {}'.format(src, dst))
        path = str(src) + ' -> '
        for node in port_path:
            path += 's{}:{}'.format(*node) + ' -> '
        path += str(dst)
        self.logger.info(path)
        self.logger.info('\n')