/*
 * PocketDAPNET - open-source POCSAG transceiver and DAPNET node firmware
 * Copyright (C) 2026 DM1PWN and contributors
 * SPDX-License-Identifier: GPL-3.0-or-later
 */

#pragma once

#define DEFAULT_POCSAG_FREQUENCY_MHZ 439.9875f
#define DEFAULT_POCSAG_BAUD          1200
#define DEFAULT_POCSAG_SHIFT_HZ      4500
#define DEFAULT_TX_POWER_DBM          10
#define DEFAULT_POCSAG_INVERT        false
#define DEFAULT_RX_FREQ_CORR_MHZ     0.0000f
#define DEFAULT_TX_FREQ_CORR_MHZ     0.0000f
#define DEFAULT_OWN_RIC              0u
#define DEFAULT_RX_RICS              ""

#define DEFAULT_WIFI_MODE            "client"
#define DEFAULT_AP_SSID              "PocketDAPNET"
#define DEFAULT_AP_PASSWORD          "dapnet433"
#define DEFAULT_STA_TIMEOUT_MS       15000UL

#define DEFAULT_TX_INHIBIT           true

#define DEFAULT_WEB_USERNAME         "admin"
#define DEFAULT_WEB_PASSWORD         "pocketdapnet"
#define DEFAULT_API_ENABLED          false
#define DEFAULT_API_TOKEN            ""
#define DEFAULT_WEBHOOK_ENABLED       false
#define DEFAULT_WEBHOOK_URL           ""

#define DEFAULT_STATION_ID_ENABLED   true
#define DEFAULT_STATION_ID_INTERVAL_MIN 10
#define DEFAULT_STATION_ID_RIC       8u
#define DEFAULT_LED_MODE             "notifications"

#define DEFAULT_NTP_PROVIDER         "pool"
#define DEFAULT_NTP_CUSTOM_SERVER    ""

#define DEFAULT_DAPNET_ENABLED       false
#define DEFAULT_DAPNET_HOST          ""
#define DEFAULT_DAPNET_PORT          43434
#define DEFAULT_DAPNET_CALLSIGN      ""
#define DEFAULT_DAPNET_AUTHKEY       ""
#define DEFAULT_DAPNET_TIMESLOTS     ""

