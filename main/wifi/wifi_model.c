#include "wifi_model.h"
#include "wifi_scan_worker.h"
#include "view_event.h"
#include "esp_log.h"

#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_netif.h"

#include "lwip/err.h"
#include "lwip/sys.h"
#include "lwip/inet.h"
#include "lwip/netdb.h"
#include "lwip/sockets.h"
#include "esp_event.h"
#include "ping/ping_sock.h"

#define WIFI_CONNECTED_BIT BIT0
#define WIFI_FAIL_BIT	   BIT1

struct indicator_wifi
{
	struct view_data_wifi_st st;
	bool is_cfg;
	int wifi_reconnect_cnt;
};

static struct indicator_wifi _g_wifi_model;
static SemaphoreHandle_t _g_wifi_mutex;
static SemaphoreHandle_t _g_data_mutex;
static SemaphoreHandle_t _g_net_check_sem;

static int s_retry_num = 0;
static int wifi_retry_max = 3;
static bool _g_ping_done = true;

static EventGroupHandle_t _wifi_event_group;

static const char* TAG = "wifi-model";

static void _wifi_st_set(struct view_data_wifi_st* p_st) {
	xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
	memcpy(&_g_wifi_model.st, p_st, sizeof(struct view_data_wifi_st));
	xSemaphoreGive(_g_data_mutex);
}

static void _wifi_st_get(struct view_data_wifi_st* p_st) {
	xSemaphoreTake(_g_data_mutex, portMAX_DELAY);
	memcpy(p_st, &_g_wifi_model.st, sizeof(struct view_data_wifi_st));
	xSemaphoreGive(_g_data_mutex);
}

static void _wifi_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	switch(event_id)
	{
		case WIFI_EVENT_STA_START:
		{
			ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_START");
			struct view_data_wifi_st st;
			st.is_connected = false;
			st.is_network = false;
			st.is_connecting = true;
			memset(st.ssid, 0, sizeof(st.ssid));
			st.rssi = 0;
			_wifi_st_set(&st);

			esp_wifi_connect();
			break;
		}
		case WIFI_EVENT_STA_CONNECTED:
		{
			ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_CONNECTED");
			wifi_event_sta_connected_t* event = (wifi_event_sta_connected_t*)event_data;
			struct view_data_wifi_st st;

			_wifi_st_get(&st);
			memset(st.ssid, 0, sizeof(st.ssid));
			memcpy(st.ssid, event->ssid, event->ssid_len);
			st.rssi = -50; // todo
			st.is_connected = true;
			st.is_connecting = false;
			_wifi_st_set(&st);

			view_event_publish_state(
				VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st));

			struct view_data_wifi_connet_ret_msg msg;
			msg.ret = 0;
			strcpy(msg.msg, "Connection successful");
			view_event_delivery_t delivery =
				view_event_post_command(VIEW_EVENT_WIFI_CONNECT_RET, &msg, sizeof(msg));
			if(delivery != VIEW_EVENT_DELIVERY_ENQUEUED)
			{
				ESP_LOGE(TAG, "wifi connect result rejected: delivery=%d", (int)delivery);
			}
			break;
		}
		case WIFI_EVENT_STA_DISCONNECTED:
		{
			ESP_LOGI(TAG, "wifi event: WIFI_EVENT_STA_DISCONNECTED");

			if((wifi_retry_max == -1) || s_retry_num < wifi_retry_max)
			{
				esp_wifi_connect();
				s_retry_num++;
				ESP_LOGI(TAG, "retry to connect to the AP");
			}
			else
			{
				struct view_data_wifi_st st;

				_wifi_st_get(&st);
				st.is_connected = false;
				st.is_network = false;
				st.is_connecting = false;
				_wifi_st_set(&st);

				view_event_publish_state(
					VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st));

				struct view_data_wifi_connet_ret_msg msg;
				msg.ret = 1;
				strcpy(msg.msg, "Connection failure");
				view_event_delivery_t delivery =
					view_event_post_command(VIEW_EVENT_WIFI_CONNECT_RET, &msg, sizeof(msg));
				if(delivery != VIEW_EVENT_DELIVERY_ENQUEUED)
				{
					ESP_LOGE(TAG, "wifi failure result rejected: delivery=%d", (int)delivery);
				}
			}
			break;
		}
		default:
			break;
	}
}

static void _ip_event_handler(void* arg, esp_event_base_t event_base, int32_t event_id, void* event_data) {
	if(event_id == IP_EVENT_STA_GOT_IP)
	{
		ip_event_got_ip_t* event = (ip_event_got_ip_t*)event_data;
		ESP_LOGI(TAG, "got ip:" IPSTR, IP2STR(&event->ip_info.ip));
		s_retry_num = 0;

		xSemaphoreGive(_g_net_check_sem);
	}
}

static int _wifi_connect(const char* p_ssid, const char* p_password, int retry_num) {
	wifi_retry_max = retry_num;
	s_retry_num = 0;

	wifi_config_t wifi_config = {0};
	strlcpy((char*)wifi_config.sta.ssid, p_ssid, sizeof(wifi_config.sta.ssid));
	ESP_LOGI(TAG, "ssid: %s", p_ssid);
	if(p_password)
	{
		ESP_LOGI(TAG, "password: %s", p_password);
		strlcpy((char*)wifi_config.sta.password, p_password, sizeof(wifi_config.sta.password));
		wifi_config.sta.threshold.authmode = WIFI_AUTH_WPA2_PSK;
	}
	else
	{
		wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
	}
	wifi_config.sta.sae_pwe_h2e = WPA3_SAE_PWE_BOTH;

	esp_wifi_stop();
	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));

	_g_wifi_model.is_cfg = true;

	struct view_data_wifi_st st = {0};
	st.is_connected = false;
	st.is_connecting = false;
	st.is_network = false;
	_wifi_st_set(&st);

	ESP_ERROR_CHECK(esp_wifi_start());

	ESP_LOGI(TAG, "connect...");
	return 0;
}

static void _wifi_cfg_restore(void) {
	_g_wifi_model.is_cfg = false;

	struct view_data_wifi_st st = {0};
	st.is_connected = false;
	st.is_connecting = false;
	st.is_network = false;
	_wifi_st_set(&st);

	view_event_publish_state(
		VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st));

	esp_wifi_restore();
}

static void _wifi_shutdown(void) {
	_g_wifi_model.is_cfg = false;

	struct view_data_wifi_st st = {0};
	st.is_connected = false;
	st.is_connecting = false;
	st.is_network = false;
	_wifi_st_set(&st);

	view_event_publish_state(
		VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st));

	esp_wifi_stop();
}

static void _ping_end(esp_ping_handle_t hdl, void* args) {
	ip_addr_t target_addr;
	uint32_t transmitted = 0;
	uint32_t received = 0;
	uint32_t total_time_ms = 0;
	uint32_t loss = 0;
	esp_ping_get_profile(hdl, ESP_PING_PROF_REQUEST, &transmitted, sizeof(transmitted));
	esp_ping_get_profile(hdl, ESP_PING_PROF_REPLY, &received, sizeof(received));
	esp_ping_get_profile(hdl, ESP_PING_PROF_IPADDR, &target_addr, sizeof(target_addr));
	esp_ping_get_profile(hdl, ESP_PING_PROF_DURATION, &total_time_ms, sizeof(total_time_ms));

	if(transmitted > 0)
	{
		loss = (uint32_t)((1 - ((float)received) / transmitted) * 100);
	}
	else
	{
		loss = 100;
	}

	if(IP_IS_V4(&target_addr))
	{
		printf("\n--- %s ping statistics ---\n", inet_ntoa(*ip_2_ip4(&target_addr)));
	}
	else
	{
		printf("\n--- %s ping statistics ---\n", inet6_ntoa(*ip_2_ip6(&target_addr)));
	}
	printf("%d packets transmitted, %d received, %d%% packet loss, time %dms\n", transmitted, received, loss,
		   total_time_ms);

	esp_ping_delete_session(hdl);

	struct view_data_wifi_st st;
	if(received > 0)
	{
		_wifi_st_get(&st);
		st.is_network = true;
		_wifi_st_set(&st);
	}
	else
	{
		_wifi_st_get(&st);
		st.is_network = false;
		_wifi_st_set(&st);
	}
	view_event_publish_state(
		VIEW_EVENT_WIFI_ST, &st, sizeof(struct view_data_wifi_st));
	_g_ping_done = true;
}

static void _ping_start(void) {
	esp_ping_config_t config = ESP_PING_DEFAULT_CONFIG();

	ip_addr_t target_addr;
	ipaddr_aton("1.1.1.1", &target_addr);

	config.target_addr = target_addr;

	esp_ping_callbacks_t cbs = {
		.cb_args = NULL, .on_ping_success = NULL, .on_ping_timeout = NULL, .on_ping_end = _ping_end};
	esp_ping_handle_t ping;
	esp_ping_new_session(&config, &cbs, &ping);
	_g_ping_done = false;
	esp_ping_start(ping);
}

static void _indicator_wifi_task(void* p_arg) {
	int cnt = 0;
	struct view_data_wifi_st st;

	while(1)
	{
		xSemaphoreTake(_g_net_check_sem, pdMS_TO_TICKS(5000));
		_wifi_st_get(&st);

		if(st.is_connected)
		{
			if(_g_ping_done)
			{
				if(st.is_network)
				{
					cnt++;
					if(cnt > 60)
					{
						cnt = 0;
						ESP_LOGI(TAG, "Network normal last time, retry check network...");
						_ping_start();
					}
				}
				else
				{
					ESP_LOGI(TAG, "Last network exception, check network...");
					_ping_start();
				}
			}
		}
		else if(_g_wifi_model.is_cfg && !st.is_connecting)
		{
			if(_g_wifi_model.wifi_reconnect_cnt > 5)
			{
				ESP_LOGI(TAG, " Wifi reconnect...");
				_g_wifi_model.wifi_reconnect_cnt = 0;
				wifi_retry_max = 3;
				s_retry_num = 0;

				esp_wifi_stop();
				ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
				ESP_ERROR_CHECK(esp_wifi_start());
			}
			_g_wifi_model.wifi_reconnect_cnt++;
		}
	}
}

static void _view_event_handler(
	void* handler_args, enum view_event_id id, const void* event_data) {
	switch(id)
	{
		case VIEW_EVENT_WIFI_LIST_REQ:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_LIST_REQ");
			struct view_data_wifi_st st;
			_wifi_st_get(&st);
			if(!wifi_scan_worker_request(&st))
			{
				ESP_LOGE(TAG, "wifi scan request rejected by worker");
			}
			break;
		}
		case VIEW_EVENT_WIFI_CONNECT:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CONNECT");
			struct view_data_wifi_config* p_cfg = (struct view_data_wifi_config*)event_data;

			if(p_cfg->have_password)
			{
				_wifi_connect(p_cfg->ssid, p_cfg->password, 3);
			}
			else
			{
				_wifi_connect(p_cfg->ssid, NULL, 3);
			}
			break;
		}
		case VIEW_EVENT_WIFI_CFG_DELETE:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_WIFI_CFG_DELETE");
			_wifi_cfg_restore();
			break;
		}
		case VIEW_EVENT_SHUTDOWN:
		{
			ESP_LOGI(TAG, "event: VIEW_EVENT_SHUTDOWN");
			_wifi_shutdown();
			break;
		}
		default:
			break;
	}
}

int indicator_wifi_model_init(void) {
	_g_wifi_mutex = xSemaphoreCreateMutex();
	_g_data_mutex = xSemaphoreCreateMutex();
	_g_net_check_sem = xSemaphoreCreateBinary();

	memset(&_g_wifi_model, 0, sizeof(_g_wifi_model));

	xTaskCreate(&_indicator_wifi_task, "_indicator_wifi_task", 1024 * 5, NULL, 10, NULL);

	ESP_ERROR_CHECK(esp_netif_init());
	ESP_ERROR_CHECK(esp_event_loop_create_default());

	esp_netif_create_default_wifi_sta();

	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	ESP_ERROR_CHECK(esp_wifi_init(&cfg));
	ESP_ERROR_CHECK(wifi_scan_worker_init() ? ESP_OK : ESP_FAIL);

	esp_event_handler_instance_t instance_any_id;
	esp_event_handler_instance_t instance_got_ip;

	ESP_ERROR_CHECK(
		esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &_wifi_event_handler, 0, &instance_any_id));

	ESP_ERROR_CHECK(
		esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &_ip_event_handler, 0, &instance_got_ip));

	ESP_ERROR_CHECK(view_event_subscribe(
		VIEW_EVENT_WIFI_LIST_REQ, _view_event_handler, NULL) ? ESP_OK : ESP_FAIL);
	ESP_ERROR_CHECK(view_event_subscribe(
		VIEW_EVENT_WIFI_CONNECT, _view_event_handler, NULL) ? ESP_OK : ESP_FAIL);
	ESP_ERROR_CHECK(view_event_subscribe(
		VIEW_EVENT_WIFI_CFG_DELETE, _view_event_handler, NULL) ? ESP_OK : ESP_FAIL);
	ESP_ERROR_CHECK(view_event_subscribe(
		VIEW_EVENT_SHUTDOWN, _view_event_handler, NULL) ? ESP_OK : ESP_FAIL);

	wifi_config_t wifi_cfg;
	esp_wifi_get_config(WIFI_IF_STA, &wifi_cfg);

	if(strlen((const char*)wifi_cfg.sta.ssid))
	{
		_g_wifi_model.is_cfg = true;
		ESP_LOGI(TAG, "last config ssid: %s", wifi_cfg.sta.ssid);
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_start());
	}
	else
	{
		ESP_LOGI(TAG, "Not config wifi, Entry wifi config screen");
		uint8_t screen = SCREEN_WIFI_CONFIG;
		ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
		ESP_ERROR_CHECK(esp_wifi_start());
		view_event_delivery_t delivery = view_event_post_command(
			VIEW_EVENT_SCREEN_START, &screen, sizeof(screen));
		if(delivery != VIEW_EVENT_DELIVERY_ENQUEUED)
		{
			ESP_LOGE(TAG, "wifi setup screen command rejected: delivery=%d", (int)delivery);
		}
	}

	return 0;
}
