from ryu.base import app_manager
from ryu.controller import ofp_event
from ryu.controller.handler import CONFIG_DISPATCHER, MAIN_DISPATCHER
from ryu.controller.handler import set_ev_cls
from ryu.ofproto import ofproto_v1_3
from ryu.lib.packet import packet
from ryu.lib.packet import ethernet
from ryu.lib.packet import ether_types
from ryu.lib.packet import ipv4
from ryu.lib.packet import tcp

class RyuRedirect(app_manager.RyuApp):
    OFP_VERSIONS = [ofproto_v1_3.OFP_VERSION]

    # IP Definitions
    CLIENT_IP = '10.0.1.5'
    SERVER1_IP = '10.0.1.2'
    SERVER2_IP = '10.0.1.3'

    # MAC Definitions
    CLIENT_MAC = '00:00:00:00:00:03'
    SERVER1_MAC = '00:00:00:00:00:01'
    SERVER2_MAC = '00:00:00:00:00:02'

    def __init__(self, *args, **kwargs):
        super(RyuRedirect, self).__init__(*args, **kwargs)
        self.mac_to_port = {}
        # Static port mapping based on networkTopo.py link order
        # Client -> Port 1, Server1 -> Port 2, Server2 -> Port 3
        self.static_host_ports = {
            self.CLIENT_MAC: 1,
            self.SERVER1_MAC: 2,
            self.SERVER2_MAC: 3
        }

    @set_ev_cls(ofp_event.EventOFPSwitchFeatures, CONFIG_DISPATCHER)
    def switch_features_handler(self, ev):
        datapath = ev.msg.datapath
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser

        # Install Table-miss flow entry (send to controller)
        match = parser.OFPMatch()
        actions = [parser.OFPActionOutput(ofproto.OFPP_CONTROLLER,
                                          ofproto.OFPCML_NO_BUFFER)]
        self.add_flow(datapath, 0, match, actions)

    def add_flow(self, datapath, priority, match, actions, buffer_id=None, idle_timeout=0):
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser

        inst = [parser.OFPInstructionActions(ofproto.OFPIT_APPLY_ACTIONS,
                                             actions)]

        if buffer_id:
            mod = parser.OFPFlowMod(datapath=datapath, buffer_id=buffer_id,
                                    priority=priority, match=match,
                                    instructions=inst,
                                    idle_timeout=idle_timeout)
        else:
            mod = parser.OFPFlowMod(datapath=datapath, priority=priority,
                                    match=match, instructions=inst,
                                    idle_timeout=idle_timeout)
        datapath.send_msg(mod)

    @set_ev_cls(ofp_event.EventOFPPacketIn, MAIN_DISPATCHER)
    def _packet_in_handler(self, ev):
        msg = ev.msg
        datapath = msg.datapath
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser
        in_port = msg.match['in_port']

        pkt = packet.Packet(msg.data)
        eth_pkt = pkt.get_protocols(ethernet.ethernet)[0]

        # Ignore LLDP
        if eth_pkt.ethertype == ether_types.ETH_TYPE_LLDP:
            return

        dst = eth_pkt.dst
        src = eth_pkt.src
        dpid = datapath.id
        self.mac_to_port.setdefault(dpid, {})

        # Standard MAC Learning
        self.mac_to_port[dpid][src] = in_port

        # Parse Protocols
        ipv4_pkt = pkt.get_protocol(ipv4.ipv4)
        tcp_pkt = pkt.get_protocol(tcp.tcp)

        # --- Task 5: Redirection Logic ---
        # Detect Client -> Server1 TCP SYN
        if self._is_client_syn_to_server1(ipv4_pkt, tcp_pkt):
            print(f"Detect SYN from {self.CLIENT_IP} to {self.SERVER1_IP}. Redirecting to {self.SERVER2_IP}...")
            
            s2_port = self.static_host_ports[self.SERVER2_MAC]
            client_port = self.static_host_ports[self.CLIENT_MAC]

            # 1. Install Upstream Flow (Client -> Server1 ==> Redirect -> Server2)
            # Match: IP Src=Client, IP Dst=Server1
            # Action: Set DstMac=S2, Set DstIP=S2, Output=Port3
            match_up = parser.OFPMatch(
                in_port=client_port,
                eth_type=ether_types.ETH_TYPE_IP,
                ipv4_src=self.CLIENT_IP,
                ipv4_dst=self.SERVER1_IP
            )
            actions_up = [
                parser.OFPActionSetField(eth_dst=self.SERVER2_MAC),
                parser.OFPActionSetField(ipv4_dst=self.SERVER2_IP),
                parser.OFPActionOutput(s2_port)
            ]
            self.add_flow(datapath, 20, match_up, actions_up, idle_timeout=5)

            # 2. Install Downstream Flow (Server2 -> Client ==> Rewrite -> Server1)
            # Match: IP Src=Server2, IP Dst=Client
            # Action: Set SrcMac=S1, Set SrcIP=S1, Output=Port1
            # Note: We must rewrite the source so Client thinks reply is from Server1
            match_down = parser.OFPMatch(
                in_port=s2_port,
                eth_type=ether_types.ETH_TYPE_IP,
                ipv4_src=self.SERVER2_IP,
                ipv4_dst=self.CLIENT_IP
            )
            actions_down = [
                parser.OFPActionSetField(eth_src=self.SERVER1_MAC),
                parser.OFPActionSetField(ipv4_src=self.SERVER1_IP),
                parser.OFPActionOutput(client_port)
            ]
            self.add_flow(datapath, 20, match_down, actions_down, idle_timeout=5)

            # 3. Packet Out the current SYN packet immediately with Upstream actions
            # We must apply the actions manually to this buffered packet
            self._send_packet_out(datapath, msg, in_port, actions_up)
            return

        # --- Normal Switching Logic ---
        # (Used for ARP, Ping, and non-redirected traffic)
        
        # Determine output port
        if dst in self.mac_to_port[dpid]:
            out_port = self.mac_to_port[dpid][dst]
        else:
            out_port = ofproto.OFPP_FLOOD

        actions = [parser.OFPActionOutput(out_port)]

        # Avoid installing a flow that conflicts with our redirection
        # Only install standard L2 flows if it's NOT the redirect pair
        is_redirect_path = (src == self.CLIENT_MAC and dst == self.SERVER1_MAC) or \
                           (src == self.SERVER2_MAC and dst == self.CLIENT_MAC)

        if out_port != ofproto.OFPP_FLOOD and not is_redirect_path:
            match = parser.OFPMatch(in_port=in_port, eth_dst=dst, eth_src=src)
            # Standard flow with idle_timeout=5 as per Task 2
            if msg.buffer_id != ofproto.OFP_NO_BUFFER:
                self.add_flow(datapath, 1, match, actions, msg.buffer_id, idle_timeout=5)
                return
            else:
                self.add_flow(datapath, 1, match, actions, idle_timeout=5)

        self._send_packet_out(datapath, msg, in_port, actions)

    def _is_client_syn_to_server1(self, ipv4_pkt, tcp_pkt):
        """Check if packet is TCP SYN from Client to Server1"""
        if not ipv4_pkt or not tcp_pkt:
            return False
        if ipv4_pkt.src != self.CLIENT_IP or ipv4_pkt.dst != self.SERVER1_IP:
            return False
        # Check TCP Flags (SYN=1, ACK=0)
        flags = tcp_pkt.bits
        is_syn = (flags & tcp.TCP_SYN)
        has_ack = (flags & tcp.TCP_ACK)
        return is_syn and not has_ack

    def _send_packet_out(self, datapath, msg, in_port, actions):
        parser = datapath.ofproto_parser
        ofproto = datapath.ofproto
        data = None
        if msg.buffer_id == ofproto.OFP_NO_BUFFER:
            data = msg.data
        
        out = parser.OFPPacketOut(datapath=datapath,
                                  buffer_id=msg.buffer_id,
                                  in_port=in_port,
                                  actions=actions,
                                  data=data)
        datapath.send_msg(out)