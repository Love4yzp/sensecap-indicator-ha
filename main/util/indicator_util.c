#include "indicator_util.h"
#include <esp_system.h>
#include <esp_log.h>
#include <regex.h>

static const char* TAG = "indicator-utils";

int wifi_rssi_level_get(int rssi) {
	//    0    rssi<=-100
	//    1    (-100, -88]
	//    2    (-88, -77]
	//    3    (-66, -55]
	//    4    rssi>=-55
	if(rssi > -66)
	{
		return 3;
	}
	else if(rssi > -88)
	{
		return 2;
	}
	else
	{
		return 1;
	}
}

bool is_valid_ipv4(const char* ip_address) {
	regex_t regex;
	int reti;
	bool is_valid = false;

	// IPv4 address pattern
	// Matches numbers 0-255 for each octet
	const char* pattern =
		"^([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.([0-9]|"
		"[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])\\.([0-9]|[1-9][0-9]|1[0-9]{2}|2[0-4][0-9]|25[0-5])$";

	// Compile regular expression
	reti = regcomp(&regex, pattern, REG_EXTENDED);
	if(reti)
	{
		ESP_LOGE(TAG, "Could not compile regex");
		return false;
	}

	// Execute regular expression
	reti = regexec(&regex, ip_address, 0, NULL, 0);
	if(!reti)
	{
		is_valid = true;
	}

	// Free compiled regular expression
	regfree(&regex);

	return is_valid;
}

bool extract_ip_from_url(const char* url, char* ip, size_t ip_size) {
	regex_t regex;
	regmatch_t matches[2];
	const char* pattern = "([0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)";

	if(regcomp(&regex, pattern, REG_EXTENDED) != 0)
	{
		ESP_LOGE(TAG, "Failed to compile regex");
		return false;
	}

	if(regexec(&regex, url, 2, matches, 0) == 0)
	{
		size_t len = matches[1].rm_eo - matches[1].rm_so;
		if(len < ip_size)
		{
			strncpy(ip, url + matches[1].rm_so, len);
			ip[len] = '\0';
			regfree(&regex);
			return true;
		}
	}

	regfree(&regex);
	return false;
}

void assemble_broker_url(const char* ip_address, char* broker_url, size_t broker_url_size) {
	const char* prefix = "mqtt://"; // MQTT Protocol prefix
	const char* suffix = ":1883"; // MQTT The default port
	//const char* suffix = ""; // The default port

	// 组装成完整的 broker URL，确保总长度不超过目标数组的大小
	snprintf(broker_url, broker_url_size, "%s%s%s", prefix, ip_address, suffix);
}
