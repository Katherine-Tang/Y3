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


class RyuForward(app_manager.RyuApp):
    OFP_VERSIONS = [ofproto_v1_3.OFP_VERSION]

    # Network configuration constants
    CLIENT_IP = '10.0.1.5'
    SERVER1_IP = '10.0.1.2'
    CLIENT_MAC = '00:00:00:00:00:03'
    SERVER1_MAC = '00:00:00:00:00:01'
    SERVER2_MAC = '00:00:00:00:00:02'

    def __init__(self, *args, **kwargs):
        super(RyuForward, self).__init__(*args, **kwargs)
        self.mac_to_port = {}
        
        # Pre-define static ports based on the Mininet topology order.
        # This ensures we know where hosts are attached even before they send traffic.
        self.static_host_ports = {
            self.CLIENT_MAC: 1,
            self.SERVER1_MAC: 2,
            self.SERVER2_MAC: 3
        }

    @set_ev_cls(ofp_event.EventOFPSwitchFeatures, CONFIG_DISPATCHER)
    def switch_features_handler(self, ev):
        """
        Invoked when the switch connects to the controller.
        Installs the 'Table-miss' flow entry to send unmatched packets to the controller.
        """
        datapath = ev.msg.datapath
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser

        match = parser.OFPMatch()
        actions = [parser.OFPActionOutput(ofproto.OFPP_CONTROLLER,
                                          ofproto.OFPCML_NO_BUFFER)]
        self.add_flow(datapath, 0, match, actions)

    def add_flow(self, datapath, priority, match, actions, buffer_id=None):
        """
        Helper function to add a flow entry to the switch.
        Includes logic to handle buffered packets and timeouts.
        """
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser

        inst = [parser.OFPInstructionActions(ofproto.OFPIT_APPLY_ACTIONS,
                                             actions)]

        # Set a 5-second idle timeout for flows to prevent the table from filling up indefinitely.
        if priority > 0:
            idle_timeout = 5
            if buffer_id is not None:
                mod = parser.OFPFlowMod(datapath=datapath, buffer_id=buffer_id,
                                        priority=priority, match=match,
                                        instructions=inst,
                                        idle_timeout=idle_timeout)
            else:
                mod = parser.OFPFlowMod(datapath=datapath, priority=priority,
                                        match=match, instructions=inst,
                                        idle_timeout=idle_timeout)
        else:
            # Low priority flows (like table-miss) usually don't expire.
            if buffer_id is not None:
                mod = parser.OFPFlowMod(datapath=datapath, buffer_id=buffer_id,
                                        priority=priority, match=match,
                                        instructions=inst)
            else:
                mod = parser.OFPFlowMod(datapath=datapath, priority=priority,
                                        match=match, instructions=inst)
        datapath.send_msg(mod)

    @set_ev_cls(ofp_event.EventOFPPacketIn, MAIN_DISPATCHER)
    def _packet_in_handler(self, ev):
        """
        Handles incoming packets sent from the switch to the controller.
        """
        msg = ev.msg
        datapath = msg.datapath
        ofproto = datapath.ofproto
        parser = datapath.ofproto_parser
        in_port = msg.match['in_port']

        # Parse the packet data
        pkt = packet.Packet(msg.data)
        eth_pkt = pkt.get_protocols(ethernet.ethernet)[0]

        # Ignore LLDP packets used for topology discovery
        if eth_pkt.ethertype == ether_types.ETH_TYPE_LLDP:
            return

        dst = eth_pkt.dst
        src = eth_pkt.src

        dpid = datapath.id
        self.mac_to_port.setdefault(dpid, {})

        # Learn the MAC address and input port mapping
        self.mac_to_port[dpid][src] = in_port

        ipv4_pkt = pkt.get_protocol(ipv4.ipv4)
        tcp_pkt = pkt.get_protocol(tcp.tcp)


        # Special Logic: Bidirectional TCP Flow Installation
        if self._is_client_syn_to_server1(ipv4_pkt, tcp_pkt):
            server_port = self._resolve_host_port(dpid, self.SERVER1_MAC)
            client_port = in_port

            # 1. Configure the Forward Flow (Client -> Server1)
            actions_forward = [parser.OFPActionOutput(server_port)]
            match_forward = parser.OFPMatch(
                in_port=client_port,
                eth_type=ether_types.ETH_TYPE_IP,
                eth_src=self.CLIENT_MAC,
                eth_dst=self.SERVER1_MAC,
                ipv4_src=self.CLIENT_IP,
                ipv4_dst=self.SERVER1_IP,
                ip_proto=6, # TCP
                tcp_src=tcp_pkt.src_port,
                tcp_dst=tcp_pkt.dst_port
            )
            self.add_flow(datapath, 10, match_forward, actions_forward)

            # 2. Configure the Backward Flow (Server1 -> Client)
            # We assume the server will reply from its listening port to the client's ephemeral port.
            # Note: Source and Destination fields are swapped here compared to the forward flow.
            actions_backward = [parser.OFPActionOutput(client_port)]
            match_backward = parser.OFPMatch(
                in_port=server_port,
                eth_type=ether_types.ETH_TYPE_IP,
                eth_src=self.SERVER1_MAC,
                eth_dst=self.CLIENT_MAC,
                ipv4_src=self.SERVER1_IP,
                ipv4_dst=self.CLIENT_IP,
                ip_proto=6, # TCP
                tcp_src=tcp_pkt.dst_port, # Match Server's Source Port (Client's Dest)
                tcp_dst=tcp_pkt.src_port  # Match Server's Dest Port (Client's Source)
            )
            print(f"Installing Bidirectional TCP Flow: {self.CLIENT_IP}:{tcp_pkt.src_port} <--> {self.SERVER1_IP}:{tcp_pkt.dst_port}")
            self.add_flow(datapath, 10, match_backward, actions_backward)

            # Send the current SYN packet out to the server to start the connection
            self._send_packet_out(datapath, msg, in_port, actions_forward)
            return

        # Standard L2 Switching Logic
        if dst in self.mac_to_port[dpid]:
            out_port = self.mac_to_port[dpid][dst]
        else:
            out_port = ofproto.OFPP_FLOOD

        actions = [parser.OFPActionOutput(out_port)]

        # Determine if we should install a flow for this packet.
        # We skip installing a generic L2 flow for the Client-Server1 pair 
        # to avoid conflicts with our specific TCP logic above.
        skip_install = (src == self.CLIENT_MAC and dst == self.SERVER1_MAC)

        if out_port != ofproto.OFPP_FLOOD and not skip_install:
            match = parser.OFPMatch(in_port=in_port, eth_dst=dst, eth_src=src)
            if msg.buffer_id != ofproto.OFP_NO_BUFFER:
                self.add_flow(datapath, 1, match, actions, msg.buffer_id)
                return
            else:
                self.add_flow(datapath, 1, match, actions)

        # Forward the packet if no flow was installed or for the first packet in a flow
        self._send_packet_out(datapath, msg, in_port, actions)

    def _is_client_syn_to_server1(self, ipv4_pkt, tcp_pkt):
        """
        Checks if the packet is a TCP SYN initiating a connection from Client to Server1.
        """
        if not ipv4_pkt or not tcp_pkt:
            return False
        if ipv4_pkt.src != self.CLIENT_IP or ipv4_pkt.dst != self.SERVER1_IP:
            return False
        
        # Check TCP flags: SYN should be set, ACK should NOT be set
        flags = tcp_pkt.bits
        is_syn = flags & tcp.TCP_SYN
        has_ack = flags & tcp.TCP_ACK
        return is_syn and not has_ack

    def _resolve_host_port(self, dpid, mac_addr):
        """
        Finds the output port for a MAC address, checking learned MACs first,
        then falling back to the static configuration.
        """
        port = self.mac_to_port.get(dpid, {}).get(mac_addr)
        if port is None:
            port = self.static_host_ports.get(mac_addr)
        return port

    def _send_packet_out(self, datapath, msg, in_port, actions):
        """
        Constructs and sends a PacketOut message to the switch.
        """
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