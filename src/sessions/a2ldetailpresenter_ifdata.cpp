#include "sessions/a2ldetailpresenter.h"

QList<DetailSection> A2lDetailPresenter::xcpSummaryDetails(const A2lPath& path) const {
    QList<DetailSection> sections;
    const auto* module = moduleAt(path.primaryIndex);
    if (!module) {
        return sections;
    }

    for (const auto& ifData : module->if_datas()) {
        if (!ifData.has_xcp()) {
            continue;
        }

        const auto& xcp = ifData.xcp();

        // Protocol Layer
        if (xcp.has_protocol_layer()) {
            const auto& pl = xcp.protocol_layer();
            QList<DetailField> fields;
            addField(fields, QStringLiteral("Protocol Version"), hex64(pl.protocol_version()));
            addNumberField(fields, QStringLiteral("T1 (ms)"), pl.t1_ms());
            addNumberField(fields, QStringLiteral("T2 (ms)"), pl.t2_ms());
            addNumberField(fields, QStringLiteral("T3 (ms)"), pl.t3_ms());
            addNumberField(fields, QStringLiteral("T4 (ms)"), pl.t4_ms());
            addNumberField(fields, QStringLiteral("T5 (ms)"), pl.t5_ms());
            addNumberField(fields, QStringLiteral("T6 (ms)"), pl.t6_ms());
            addNumberField(fields, QStringLiteral("T7 (ms)"), pl.t7_ms());
            addNumberField(fields, QStringLiteral("MAX_CTO"), pl.max_cto());
            addNumberField(fields, QStringLiteral("MAX_DTO"), pl.max_dto());
            addField(fields, QStringLiteral("Byte Order"), text(a2l::ByteOrder_Name(pl.byte_order())));
            addField(fields, QStringLiteral("Address Granularity"), text(a2l::AddressGranularity_Name(pl.address_granularity())));
            if (pl.optional_cmds_size() > 0) {
                addField(fields, QStringLiteral("Optional Commands"), joinNumbers(pl.optional_cmds()));
            }
            if (pl.optional_level1_cmds_size() > 0) {
                addField(fields, QStringLiteral("Optional L1 Commands"), joinNumbers(pl.optional_level1_cmds()));
            }
            addOptionalNumber(fields, QStringLiteral("Comm Mode Supported"), pl.has_communication_mode_supported(), pl.communication_mode_supported());
            addOptionalString(fields, QStringLiteral("Seed & Key Function"), pl.has_seed_and_key_external_function(), pl.seed_and_key_external_function());
            addOptionalNumber(fields, QStringLiteral("MAX_DTO_STIM"), pl.has_max_dto_stim(), pl.max_dto_stim());
            pushSection(sections, QStringLiteral("Protocol Layer"), std::move(fields));
        }

        // DAQ
        if (xcp.has_daq()) {
            const auto& daq = xcp.daq();
            QList<DetailField> fields;
            addField(fields, QStringLiteral("Config Type"), text(a2l::DaqConfigType_Name(daq.daq_config_type())));
            addNumberField(fields, QStringLiteral("MAX_DAQ"), daq.max_daq());
            addNumberField(fields, QStringLiteral("MAX_EVENT_CHANNEL"), daq.max_event_channel());
            addNumberField(fields, QStringLiteral("MIN_DAQ"), daq.min_daq());
            addField(fields, QStringLiteral("Optimisation Type"), text(a2l::DaqOptimisationType_Name(daq.optimisation_type())));
            addField(fields, QStringLiteral("Address Extension"), text(a2l::DaqAddressExtensionMode_Name(daq.address_extension_mode())));
            addField(fields, QStringLiteral("ID Field Type"), text(a2l::IdentificationFieldType_Name(daq.identification_field_type())));
            addNumberField(fields, QStringLiteral("Granularity ODT Entry Size DAQ"), daq.granularity_odt_entry_size_daq());
            addNumberField(fields, QStringLiteral("MAX_ODT_ENTRY_SIZE_DAQ"), daq.max_odt_entry_size_daq());
            addField(fields, QStringLiteral("Overload Indication"), text(a2l::OverloadIndication_Name(daq.overload_indication())));
            addOptionalBool(fields, QStringLiteral("Prescaler Supported"), daq.has_prescaler_supported(), daq.prescaler_supported());
            addOptionalBool(fields, QStringLiteral("Resume Supported"), daq.has_resume_supported(), daq.resume_supported());
            addOptionalBool(fields, QStringLiteral("Store DAQ Supported"), daq.has_store_daq_supported(), daq.store_daq_supported());

            if (daq.has_stim()) {
                addNumberField(fields, QStringLiteral("STIM Granularity"), daq.stim().granularity_odt_entry_size_stim());
                addNumberField(fields, QStringLiteral("STIM MAX_ODT_ENTRY_SIZE"), daq.stim().max_odt_entry_size_stim());
                addOptionalNumber(fields, QStringLiteral("STIM MIN_ST"), daq.stim().has_min_st_stim(), daq.stim().min_st_stim());
            }

            if (daq.has_timestamp_supported()) {
                const auto& ts = daq.timestamp_supported();
                addNumberField(fields, QStringLiteral("Timestamp Ticks"), ts.timestamp_ticks());
                addField(fields, QStringLiteral("Timestamp Size"), text(a2l::TimestampSize_Name(ts.timestamp_size())));
                addField(fields, QStringLiteral("Timestamp Unit"), text(a2l::TimestampUnit_Name(ts.timestamp_unit())));
                addOptionalBool(fields, QStringLiteral("Timestamp Fixed"), ts.has_timestamp_fixed(), ts.timestamp_fixed());
            }

            if (daq.has_daq_memory_consumption()) {
                const auto& mem = daq.daq_memory_consumption();
                addOptionalNumber(fields, QStringLiteral("ODT Size"), mem.has_odt_size(), mem.odt_size());
                addOptionalNumber(fields, QStringLiteral("ODT Entry Size"), mem.has_odt_entry_size(), mem.odt_entry_size());
                addOptionalNumber(fields, QStringLiteral("DAQ Size"), mem.has_daq_size(), mem.daq_size());
                addOptionalNumber(fields, QStringLiteral("DAQ Memory Limit"), mem.has_daq_memory_limit(), mem.daq_memory_limit());
            }

            pushSection(sections, QStringLiteral("DAQ"), std::move(fields));

            // DAQ Lists
            for (int i = 0; i < daq.daq_lists_size(); ++i) {
                const auto& list = daq.daq_lists(i);
                QList<DetailField> listFields;
                addNumberField(listFields, QStringLiteral("DAQ List Number"), list.daq_list_number());
                addNumberField(listFields, QStringLiteral("DAQ List Type"), list.daq_list_type());
                addNumberField(listFields, QStringLiteral("MAX_ODT"), list.max_odt());
                addNumberField(listFields, QStringLiteral("MAX_ODT_ENTRIES"), list.max_odt_entries());
                addOptionalNumber(listFields, QStringLiteral("First PID"), list.has_first_pid(), list.first_pid());
                addOptionalNumber(listFields, QStringLiteral("Event Fixed"), list.has_event_fixed(), list.event_fixed());
                pushSection(sections, QStringLiteral("DAQ List #%1").arg(list.daq_list_number()), std::move(listFields));
            }

            // Event Channels
            for (int i = 0; i < daq.events_size(); ++i) {
                const auto& ev = daq.events(i);
                QList<DetailField> evFields;
                addField(evFields, QStringLiteral("Name"), text(ev.name()));
                addField(evFields, QStringLiteral("Short Name"), text(ev.short_name()));
                addNumberField(evFields, QStringLiteral("Channel Number"), ev.event_channel_number());
                addNumberField(evFields, QStringLiteral("DAQ List Type"), ev.daq_list_type());
                addNumberField(evFields, QStringLiteral("MAX_DAQ_LIST"), ev.max_daq_list());
                addNumberField(evFields, QStringLiteral("Time Cycle"), ev.time_cycle());
                addNumberField(evFields, QStringLiteral("Time Unit"), ev.time_unit());
                addNumberField(evFields, QStringLiteral("Priority"), ev.priority());
                addOptionalNumber(evFields, QStringLiteral("Consistency"), ev.has_consistency(), ev.consistency());
                pushSection(sections, QStringLiteral("Event \"%1\" (#%2)").arg(text(ev.short_name())).arg(ev.event_channel_number()), std::move(evFields));
            }
        }

        // CAN Transports
        for (int i = 0; i < xcp.xcp_on_cans_size(); ++i) {
            const auto& can = xcp.xcp_on_cans(i);
            QList<DetailField> fields;
            addField(fields, QStringLiteral("Transport Version"), hex64(can.transport_version()));
            addOptionalNumber(fields, QStringLiteral("CAN ID Broadcast"), can.has_can_id_broadcast(), can.can_id_broadcast());
            addOptionalNumber(fields, QStringLiteral("CAN ID Master"), can.has_can_id_master(), can.can_id_master());
            addOptionalBool(fields, QStringLiteral("Master Incremental"), can.has_can_id_master_incremental(), can.can_id_master_incremental());
            addOptionalNumber(fields, QStringLiteral("CAN ID Slave"), can.has_can_id_slave(), can.can_id_slave());
            addOptionalNumber(fields, QStringLiteral("CAN ID DAQ Clock Multicast"), can.has_can_id_get_daq_clock_multicast(), can.can_id_get_daq_clock_multicast());
            addOptionalNumber(fields, QStringLiteral("Baudrate"), can.has_baudrate(), can.baudrate());
            addOptionalNumber(fields, QStringLiteral("Sample Point"), can.has_sample_point(), can.sample_point());
            if (can.has_sample_rate()) {
                addField(fields, QStringLiteral("Sample Rate"), text(a2l::SampleRate_Name(can.sample_rate())));
            }
            addOptionalNumber(fields, QStringLiteral("BTL Cycles"), can.has_btl_cycles(), can.btl_cycles());
            addOptionalNumber(fields, QStringLiteral("SJW"), can.has_sjw(), can.sjw());
            addOptionalNumber(fields, QStringLiteral("MAX_DLC_REQUIRED"), can.has_max_dlc_required(), can.max_dlc_required());
            addOptionalNumber(fields, QStringLiteral("MAX_BUS_LOAD"), can.has_max_bus_load(), can.max_bus_load());
            addOptionalBool(fields, QStringLiteral("Measurement Split Allowed"), can.has_measurement_split_allowed(), can.measurement_split_allowed());
            addOptionalString(fields, QStringLiteral("Transport Layer Instance"), can.has_transport_layer_instance(), can.transport_layer_instance());

            if (can.has_can_fd()) {
                const auto& fd = can.can_fd();
                addOptionalNumber(fields, QStringLiteral("CAN-FD MAX_DLC"), fd.has_max_dlc(), fd.max_dlc());
                addOptionalNumber(fields, QStringLiteral("CAN-FD Data Baudrate"), fd.has_can_fd_data_transfer_baudrate(), fd.can_fd_data_transfer_baudrate());
                addOptionalNumber(fields, QStringLiteral("CAN-FD Sample Point"), fd.has_sample_point(), fd.sample_point());
                addOptionalNumber(fields, QStringLiteral("CAN-FD BTL Cycles"), fd.has_btl_cycles(), fd.btl_cycles());
                addOptionalNumber(fields, QStringLiteral("CAN-FD SJW"), fd.has_sjw(), fd.sjw());
            }

            if (can.daq_list_can_ids_size() > 0) {
                for (const auto& dlc : can.daq_list_can_ids()) {
                    if (dlc.has_fixed_can_id()) {
                        addField(fields, QStringLiteral("DAQ List #%1 CAN ID").arg(dlc.daq_list_number()), hex64(dlc.fixed_can_id()));
                    } else if (dlc.has_variable_can_id()) {
                        addField(fields, QStringLiteral("DAQ List #%1 CAN ID").arg(dlc.daq_list_number()), QStringLiteral("variable"));
                    }
                }
            }

            pushSection(sections, QStringLiteral("XCP on CAN"), std::move(fields));
        }

        // TCP Transports
        for (int i = 0; i < xcp.xcp_on_tcp_ips_size(); ++i) {
            const auto& ip = xcp.xcp_on_tcp_ips(i);
            QList<DetailField> fields;
            addField(fields, QStringLiteral("Transport Version"), hex64(ip.transport_version()));
            addNumberField(fields, QStringLiteral("Port"), ip.port());
            if (ip.has_host_name()) addField(fields, QStringLiteral("Host"), text(ip.host_name()));
            if (ip.has_address()) addField(fields, QStringLiteral("Address"), text(ip.address()));
            if (ip.has_ipv6()) addField(fields, QStringLiteral("IPv6"), text(ip.ipv6()));
            addOptionalNumber(fields, QStringLiteral("MAX_BUS_LOAD"), ip.has_max_bus_load(), ip.max_bus_load());
            addOptionalNumber(fields, QStringLiteral("MAX_BIT_RATE"), ip.has_max_bit_rate(), ip.max_bit_rate());
            addOptionalNumber(fields, QStringLiteral("Packet Alignment"), ip.has_packet_alignment(), ip.packet_alignment());
            addOptionalString(fields, QStringLiteral("Transport Layer Instance"), ip.has_transport_layer_instance(), ip.transport_layer_instance());
            pushSection(sections, QStringLiteral("XCP on TCP/IP"), std::move(fields));
        }

        // UDP Transports
        for (int i = 0; i < xcp.xcp_on_udp_ips_size(); ++i) {
            const auto& ip = xcp.xcp_on_udp_ips(i);
            QList<DetailField> fields;
            addField(fields, QStringLiteral("Transport Version"), hex64(ip.transport_version()));
            addNumberField(fields, QStringLiteral("Port"), ip.port());
            if (ip.has_host_name()) addField(fields, QStringLiteral("Host"), text(ip.host_name()));
            if (ip.has_address()) addField(fields, QStringLiteral("Address"), text(ip.address()));
            if (ip.has_ipv6()) addField(fields, QStringLiteral("IPv6"), text(ip.ipv6()));
            addOptionalNumber(fields, QStringLiteral("MAX_BUS_LOAD"), ip.has_max_bus_load(), ip.max_bus_load());
            addOptionalNumber(fields, QStringLiteral("MAX_BIT_RATE"), ip.has_max_bit_rate(), ip.max_bit_rate());
            addOptionalNumber(fields, QStringLiteral("Packet Alignment"), ip.has_packet_alignment(), ip.packet_alignment());
            addOptionalString(fields, QStringLiteral("Transport Layer Instance"), ip.has_transport_layer_instance(), ip.transport_layer_instance());
            pushSection(sections, QStringLiteral("XCP on UDP/IP"), std::move(fields));
        }

        // Segments
        for (int i = 0; i < xcp.segments_size(); ++i) {
            const auto& seg = xcp.segments(i);
            QList<DetailField> segFields;
            addNumberField(segFields, QStringLiteral("Segment Number"), seg.segment_number());
            addNumberField(segFields, QStringLiteral("Page Count"), seg.page_count());
            addNumberField(segFields, QStringLiteral("Address Extension"), seg.address_extension());
            addNumberField(segFields, QStringLiteral("Compression Method"), seg.compression_method());
            addNumberField(segFields, QStringLiteral("Encryption Method"), seg.encryption_method());
            addOptionalNumber(segFields, QStringLiteral("Default Page Number"), seg.has_default_page_number(), seg.default_page_number());

            if (seg.has_checksum()) {
                const auto& cs = seg.checksum();
                addField(segFields, QStringLiteral("Checksum Type"), text(a2l::ChecksumType_Name(cs.type())));
                addOptionalNumber(segFields, QStringLiteral("Checksum MAX_BLOCK_SIZE"), cs.has_max_block_size(), cs.max_block_size());
                addOptionalString(segFields, QStringLiteral("Checksum External Function"), cs.has_external_function(), cs.external_function());
            }

            for (int p = 0; p < seg.pages_size(); ++p) {
                const auto& page = seg.pages(p);
                addNumberField(segFields, QStringLiteral("Page %1 Number").arg(p), page.page_number());
                addField(segFields, QStringLiteral("Page %1 ECU Access").arg(p), text(page.ecu_access()));
                addField(segFields, QStringLiteral("Page %1 XCP Access").arg(p), text(page.xcp_access()));
                addOptionalBool(segFields, QStringLiteral("Page %1 Init Segment").arg(p), page.has_init_segment(), page.init_segment());
            }

            for (int m = 0; m < seg.address_mappings_size(); ++m) {
                const auto& mapping = seg.address_mappings(m);
                addField(segFields, QStringLiteral("Mapping %1").arg(m),
                         QStringLiteral("%1 -> %2 (len %3)")
                             .arg(hex64(mapping.mapping_address()))
                             .arg(hex64(mapping.original_address()))
                             .arg(mapping.length()));
            }

            pushSection(sections, QStringLiteral("Segment #%1").arg(seg.segment_number()), std::move(segFields));
        }

        // DAQ Event binding (measurement-level)
        if (xcp.has_daq_event()) {
            const auto& daqEv = xcp.daq_event();
            QList<DetailField> fields;
            if (daqEv.has_fixed_event_list()) {
                addField(fields, QStringLiteral("Type"), QStringLiteral("Fixed Event List"));
                addField(fields, QStringLiteral("Event Numbers"), joinNumbers(daqEv.fixed_event_list().event_numbers()));
            }
            if (daqEv.has_variable_event_list()) {
                addField(fields, QStringLiteral("Type"), QStringLiteral("Variable Event List"));
                if (daqEv.variable_event_list().available_event_list_size() > 0) {
                    addField(fields, QStringLiteral("Available Events"), joinNumbers(daqEv.variable_event_list().available_event_list()));
                }
                if (daqEv.variable_event_list().default_event_list_size() > 0) {
                    addField(fields, QStringLiteral("Default Events"), joinNumbers(daqEv.variable_event_list().default_event_list()));
                }
            }
            pushSection(sections, QStringLiteral("DAQ Event Binding"), std::move(fields));
        }

        // Raw blocks
        if (xcp.raw_blocks_size() > 0) {
            QList<DetailField> fields;
            for (int i = 0; i < xcp.raw_blocks_size(); ++i) {
                addField(fields, QStringLiteral("Raw Block %1").arg(i), text(xcp.raw_blocks(i)));
            }
            pushSection(sections, QStringLiteral("Raw Blocks"), std::move(fields));
        }
    }

    return sections;
}

QList<DetailSection> A2lDetailPresenter::ccpSummaryDetails(const A2lPath& path) const {
    QList<DetailSection> sections;
    const auto* module = moduleAt(path.primaryIndex);
    if (!module) {
        return sections;
    }

    for (const auto& ifData : module->if_datas()) {
        if (!ifData.has_ccp()) {
            continue;
        }

        const auto& ccp = ifData.ccp();

        // TP_BLOB
        if (ccp.has_tp_blob()) {
            const auto& tp = ccp.tp_blob();
            QList<DetailField> fields;
            addField(fields, QStringLiteral("CCP Version"), hex64(tp.ccp_version()));
            addField(fields, QStringLiteral("BLOB Version"), hex64(tp.blob_version()));
            addField(fields, QStringLiteral("CRM CAN ID"), hex64(tp.crm_can_id()));
            addField(fields, QStringLiteral("DTO CAN ID"), hex64(tp.dto_can_id()));
            addNumberField(fields, QStringLiteral("Station Address"), tp.station_address());
            addField(fields, QStringLiteral("Byte Order"), text(a2l::ByteOrder_Name(tp.byte_order())));
            addOptionalNumber(fields, QStringLiteral("Baudrate"), tp.has_baudrate(), tp.baudrate());
            addOptionalNumber(fields, QStringLiteral("Sample Point"), tp.has_sample_point(), tp.sample_point());
            addOptionalNumber(fields, QStringLiteral("Sample Rate"), tp.has_sample_rate(), tp.sample_rate());
            addOptionalNumber(fields, QStringLiteral("BTL Cycles"), tp.has_btl_cycles(), tp.btl_cycles());
            addOptionalNumber(fields, QStringLiteral("SJW"), tp.has_sjw(), tp.sjw());
            if (tp.has_sync_edge()) {
                addField(fields, QStringLiteral("Sync Edge"), text(a2l::CcpSyncEdge_Name(tp.sync_edge())));
            }
            if (tp.has_daq_mode()) {
                addField(fields, QStringLiteral("DAQ Mode"), text(a2l::CcpDaqMode_Name(tp.daq_mode())));
            }
            if (tp.has_consistency()) {
                addField(fields, QStringLiteral("Consistency"), text(a2l::CcpConsistency_Name(tp.consistency())));
            }
            if (tp.has_address_extension()) {
                addField(fields, QStringLiteral("Address Extension"), text(a2l::CcpAddressExtensionMode_Name(tp.address_extension())));
            }
            if (tp.optional_cmds_size() > 0) {
                addField(fields, QStringLiteral("Optional Commands"), joinNumbers(tp.optional_cmds()));
            }

            // Defined Pages
            for (int p = 0; p < tp.defined_pages_size(); ++p) {
                const auto& page = tp.defined_pages(p);
                addNumberField(fields, QStringLiteral("Page %1 Number").arg(p), page.page_number());
                addField(fields, QStringLiteral("Page %1 Name").arg(p), text(page.name()));
                addNumberField(fields, QStringLiteral("Page %1 Address Extension").arg(p), page.address_extension());
                addField(fields, QStringLiteral("Page %1 Address").arg(p), hex64(page.address()));
                addNumberField(fields, QStringLiteral("Page %1 Size").arg(p), page.size());
                addField(fields, QStringLiteral("Page %1 Memory Type").arg(p), text(a2l::CcpPageMemoryType_Name(page.memory_type())));
                addOptionalBool(fields, QStringLiteral("Page %1 RAM Init by ECU").arg(p), page.has_ram_init_by_ecu(), page.ram_init_by_ecu());
                addOptionalBool(fields, QStringLiteral("Page %1 RAM Init by Tool").arg(p), page.has_ram_init_by_tool(), page.ram_init_by_tool());
                addOptionalBool(fields, QStringLiteral("Page %1 Default").arg(p), page.has_default_page(), page.default_page());
            }

            if (tp.has_page_switch_method()) {
                const auto& psm = tp.page_switch_method();
                addNumberField(fields, QStringLiteral("Page Switch Version"), psm.version());
                if (psm.has_autostart_behavior()) {
                    addField(fields, QStringLiteral("Autostart"), text(a2l::CcpAutostartBehavior_Name(psm.autostart_behavior())));
                }
                if (psm.has_mailbox()) {
                    addNumberField(fields, QStringLiteral("Mailbox Version"), psm.mailbox().mailbox_version());
                    addNumberField(fields, QStringLiteral("Mailbox Setup Time (ms)"), psm.mailbox().page_setup_time_ms());
                    addField(fields, QStringLiteral("Mailbox Start Address"), hex64(psm.mailbox().start_address()));
                }
            }

            pushSection(sections, QStringLiteral("TP_BLOB"), std::move(fields));
        }

        // Sources
        for (int i = 0; i < ccp.sources_size(); ++i) {
            const auto& src = ccp.sources(i);
            QList<DetailField> fields;
            addField(fields, QStringLiteral("Name"), text(src.name()));
            addNumberField(fields, QStringLiteral("Scaling Unit"), src.scaling_unit());
            addNumberField(fields, QStringLiteral("Rate"), src.rate());

            if (src.has_qp_blob()) {
                const auto& qp = src.qp_blob();
                addNumberField(fields, QStringLiteral("QP DAQ List Number"), qp.daq_list_number());
                addNumberField(fields, QStringLiteral("QP Length"), qp.length());
                addField(fields, QStringLiteral("QP CAN ID Mode"), text(a2l::CcpCanIdMode_Name(qp.can_id_mode())));
                addOptionalNumber(fields, QStringLiteral("QP Fixed CAN ID"), qp.has_fixed_can_id(), qp.fixed_can_id());
                addOptionalNumber(fields, QStringLiteral("QP First PID"), qp.has_first_pid(), qp.first_pid());
                addOptionalBool(fields, QStringLiteral("QP Reduction Allowed"), qp.has_reduction_allowed(), qp.reduction_allowed());
                if (qp.raster_ids_size() > 0) {
                    addField(fields, QStringLiteral("QP Raster IDs"), joinNumbers(qp.raster_ids()));
                }
            }

            pushSection(sections, QStringLiteral("Source \"%1\"").arg(text(src.name())), std::move(fields));
        }

        // Rasters
        for (int i = 0; i < ccp.rasters_size(); ++i) {
            const auto& raster = ccp.rasters(i);
            QList<DetailField> fields;
            addField(fields, QStringLiteral("Name"), text(raster.name()));
            addField(fields, QStringLiteral("Short Name"), text(raster.short_name()));
            addNumberField(fields, QStringLiteral("Raster ID"), raster.raster_id());
            addNumberField(fields, QStringLiteral("Scaling Unit"), raster.scaling_unit());
            addNumberField(fields, QStringLiteral("Rate"), raster.rate());
            pushSection(sections, QStringLiteral("Raster \"%1\" (#%2)").arg(text(raster.short_name())).arg(raster.raster_id()), std::move(fields));
        }

        // Event Groups
        for (int i = 0; i < ccp.event_groups_size(); ++i) {
            const auto& eg = ccp.event_groups(i);
            QList<DetailField> fields;
            addField(fields, QStringLiteral("Name"), text(eg.name()));
            addField(fields, QStringLiteral("Short Name"), text(eg.short_name()));
            if (eg.raster_ids_size() > 0) {
                addField(fields, QStringLiteral("Raster IDs"), joinNumbers(eg.raster_ids()));
            }
            pushSection(sections, QStringLiteral("Event Group \"%1\"").arg(text(eg.short_name())), std::move(fields));
        }

        // KP_BLOB
        if (ccp.has_kp_blob()) {
            const auto& kp = ccp.kp_blob();
            QList<DetailField> fields;
            addNumberField(fields, QStringLiteral("Address Extension"), kp.address_extension());
            addField(fields, QStringLiteral("Address"), hex64(kp.address()));
            addNumberField(fields, QStringLiteral("Size"), kp.size());
            if (kp.raster_ids_size() > 0) {
                addField(fields, QStringLiteral("Raster IDs"), joinNumbers(kp.raster_ids()));
            }
            pushSection(sections, QStringLiteral("KP_BLOB"), std::move(fields));
        }

        // Raw fallback blocks
        QList<DetailField> rawFields;
        addOptionalString(rawFields, QStringLiteral("Seed & Key"), ccp.has_seed_key_raw(), ccp.seed_key_raw());
        addOptionalString(rawFields, QStringLiteral("Checksum"), ccp.has_checksum_raw(), ccp.checksum_raw());
        addOptionalString(rawFields, QStringLiteral("DP_BLOB"), ccp.has_dp_blob_raw(), ccp.dp_blob_raw());
        addOptionalString(rawFields, QStringLiteral("Address Mapping"), ccp.has_addr_mapping_raw(), ccp.addr_mapping_raw());
        for (int i = 0; i < ccp.raw_blocks_size(); ++i) {
            addField(rawFields, QStringLiteral("Raw Block %1").arg(i), text(ccp.raw_blocks(i)));
        }
        pushSection(sections, QStringLiteral("Raw / Deferred"), std::move(rawFields));
    }

    return sections;
}
