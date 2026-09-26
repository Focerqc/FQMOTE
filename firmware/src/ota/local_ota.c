#include "local_ota.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <sys/param.h>

static const char *TAG = "LOCAL-OTA";

static httpd_handle_t s_http_server = NULL;
static local_ota_progress_cb_t s_progress_cb = NULL;
static local_ota_status_cb_t s_status_cb = NULL;
static bool s_ota_in_progress = false;

static const char INDEX_HTML[] =
    "<!DOCTYPE html>"
    "<html lang=\"en\">"
    "<head>"
    "<meta charset=\"UTF-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1.0\">"
    "<title>FQMOTE Wi-Fi Flasher</title>"
    "<style>"
    ":root{--bg:#0b0f19;--card:#151d30;--border:#263352;--accent:#6366f1;--text:#f8fafc;--muted:#94a3b8;}"
    "*{box-sizing:border-box;margin:0;padding:0;font-family:-apple-system,BlinkMacSystemFont,'Segoe UI',Roboto,sans-serif;}"
    "body{background:var(--bg);color:var(--text);min-height:100vh;display:flex;align-items:center;justify-content:center;padding:1.5rem;}"
    ".card{background:var(--card);border:1px solid var(--border);border-radius:1.25rem;padding:2.25rem;width:100%;max-width:480px;box-shadow:0 25px 50px -12px rgba(0,0,0,0.5);text-align:center;}"
    ".badge{display:inline-block;padding:0.35rem 0.85rem;background:rgba(99,102,241,0.15);color:#a5b4fc;border-radius:9999px;font-size:0.8rem;font-weight:600;margin-bottom:1rem;border:1px solid rgba(99,102,241,0.3);}"
    "h1{font-size:1.6rem;margin-bottom:0.4rem;font-weight:700;}"
    "p.sub{color:var(--muted);font-size:0.95rem;margin-bottom:1.75rem;}"
    ".drop-zone{border:2px dashed var(--border);border-radius:1rem;padding:2rem 1.5rem;cursor:pointer;transition:all 0.2s ease;background:rgba(255,255,255,0.02);}"
    ".drop-zone:hover,.drop-zone.dragover{border-color:var(--accent);background:rgba(99,102,241,0.08);}"
    ".icon{font-size:2.5rem;margin-bottom:0.75rem;display:block;}"
    ".file-name{font-weight:600;margin-top:0.5rem;word-break:break-all;}"
    ".file-size{color:var(--muted);font-size:0.85rem;margin-top:0.25rem;}"
    "input[type=\"file\"]{display:none;}"
    ".btn{width:100%;margin-top:1.5rem;padding:0.9rem;background:linear-gradient(135deg,#6366f1,#8b5cf6);color:#fff;border:none;border-radius:0.75rem;font-size:1rem;font-weight:600;cursor:pointer;box-shadow:0 4px 14px rgba(99,102,241,0.4);transition:all 0.2s ease;}"
    ".btn:hover:not(:disabled){opacity:0.95;transform:translateY(-1px);box-shadow:0 6px 20px rgba(99,102,241,0.5);}"
    ".btn:disabled{background:#334155;color:#64748b;box-shadow:none;cursor:not-allowed;transform:none;}"
    ".prog-wrap{margin-top:1.5rem;display:none;}"
    ".prog-bg{background:#1e293b;border-radius:9999px;height:12px;overflow:hidden;position:relative;border:1px solid var(--border);}"
    ".prog-fill{background:linear-gradient(90deg,#6366f1,#10b981);height:100%;width:0%;transition:width 0.15s ease;border-radius:9999px;}"
    ".prog-status{display:flex;justify-content:space-between;font-size:0.85rem;color:var(--muted);margin-top:0.5rem;}"
    ".banner{margin-top:1.25rem;padding:0.85rem;border-radius:0.75rem;font-size:0.9rem;display:none;}"
    ".banner.success{background:rgba(16,185,129,0.15);border:1px solid rgba(16,185,129,0.3);color:#6ee7b7;}"
    ".banner.error{background:rgba(239,68,68,0.15);border:1px solid rgba(239,68,68,0.3);color:#fca5a5;}"
    "</style>"
    "</head>"
    "<body>"
    "<div class=\"card\">"
    "<div class=\"badge\">LEAFBLASTER ESP32-S3</div>"
    "<h1>FQMOTE Wi-Fi Flasher</h1>"
    "<p class=\"sub\">Upload custom firmware wirelessly over home Wi-Fi</p>"
    "<div class=\"drop-zone\" id=\"dz\" onclick=\"document.getElementById('fi').click()\">"
    "<span class=\"icon\">&#9889;</span>"
    "<div id=\"prompt\"><strong>Click to browse</strong> or drag & drop<br><span style=\"font-size:0.85rem;color:var(--muted)\">Select firmware.bin</span></div>"
    "<div id=\"info\" style=\"display:none\">"
    "<div class=\"file-name\" id=\"fn\"></div>"
    "<div class=\"file-size\" id=\"fs\"></div>"
    "</div>"
    "</div>"
    "<input type=\"file\" id=\"fi\" accept=\".bin\">"
    "<button class=\"btn\" id=\"btn\" disabled onclick=\"upload()\">Flash Firmware</button>"
    "<div class=\"prog-wrap\" id=\"pw\">"
    "<div class=\"prog-bg\"><div class=\"prog-fill\" id=\"pf\"></div></div>"
    "<div class=\"prog-status\"><span id=\"pt\">Uploading...</span><span id=\"pct\">0%</span></div>"
    "</div>"
    "<div class=\"banner\" id=\"ban\"></div>"
    "</div>"
    "<script>"
    "let selFile=null;"
    "const fi=document.getElementById('fi'),dz=document.getElementById('dz'),btn=document.getElementById('btn');"
    "const pw=document.getElementById('pw'),pf=document.getElementById('pf'),pt=document.getElementById('pt');"
    "const pct=document.getElementById('pct'),ban=document.getElementById('ban');"
    "fi.onchange=(e)=>{if(e.target.files.length)pick(e.target.files[0]);};"
    "dz.ondragover=(e)=>{e.preventDefault();dz.classList.add('dragover');};"
    "dz.ondragleave=()=>dz.classList.remove('dragover');"
    "dz.ondrop=(e)=>{e.preventDefault();dz.classList.remove('dragover');if(e.dataTransfer.files.length)pick(e.dataTransfer.files[0]);};"
    "function fmt(b){if(b<1024)return b+' B';if(b<1048576)return(b/1024).toFixed(1)+' KB';return(b/1048576).toFixed(2)+' MB';}"
    "function pick(f){"
    "if(!f.name.endsWith('.bin')){showB('Please select a valid .bin firmware file','error');return;}"
    "selFile=f;document.getElementById('fn').textContent=f.name;document.getElementById('fs').textContent=fmt(f.size);"
    "document.getElementById('prompt').style.display='none';document.getElementById('info').style.display='block';"
    "btn.disabled=false;ban.style.display='none';"
    "}"
    "function showB(m,t){ban.textContent=m;ban.className='banner '+t;ban.style.display='block';}"
    "function upload(){"
    "if(!selFile)return;btn.disabled=true;dz.onclick=null;pw.style.display='block';ban.style.display='none';"
    "const x=new XMLHttpRequest();x.open('POST','/update',true);"
    "x.setRequestHeader('Content-Type','application/octet-stream');"
    "x.upload.onprogress=(e)=>{if(e.lengthComputable){const p=Math.round((e.loaded/e.total)*100);pf.style.width=p+'%';pct.textContent=p+'%';pt.textContent=fmt(e.loaded)+' / '+fmt(e.total);}};"
    "x.onload=()=>{if(x.status===200){pf.style.width='100%';pct.textContent='100%';pt.textContent='Complete!';showB('Firmware flashed successfully! Remote is rebooting...','success');}else{showB('Flash failed: '+(x.responseText||'Error '+x.status),'error');btn.disabled=false;}};"
    "x.onerror=()=>{showB('Connection lost during upload. Check remote screen.','error');btn.disabled=false;};"
    "x.send(selFile);"
    "}"
    "</script>"
    "</body>"
    "</html>";

static void delayed_restart_timer_callback(void *arg) {
  ESP_LOGI(TAG, "Rebooting into newly flashed firmware...");
  esp_restart();
}

static void schedule_reboot(void) {
  const esp_timer_create_args_t timer_args = {
      .callback = delayed_restart_timer_callback,
      .arg = NULL,
      .dispatch_method = ESP_TIMER_TASK,
      .name = "ota_reboot",
      .skip_unhandled_events = false,
  };
  esp_timer_handle_t timer;
  if (esp_timer_create(&timer_args, &timer) == ESP_OK) {
    esp_timer_start_once(timer, 1500000); // 1.5 seconds
  }
  else {
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
  }
}

static esp_err_t index_get_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t update_post_handler(httpd_req_t *req) {
  int total_len = req->content_len;
  ESP_LOGI(TAG, "Starting OTA POST upload. Total size: %d bytes", total_len);

  if (total_len <= 0) {
    ESP_LOGE(TAG, "Invalid content length: %d", total_len);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Content-Length required");
    return ESP_FAIL;
  }

  if (s_ota_in_progress) {
    ESP_LOGW(TAG, "OTA already in progress!");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA already in progress");
    return ESP_FAIL;
  }

  const esp_partition_t *update_partition = esp_ota_get_next_update_partition(NULL);
  if (update_partition == NULL) {
    ESP_LOGE(TAG, "Failed to determine next update partition!");
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "No OTA partition found");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "Writing to partition subtype %d at offset 0x%lx (size: %lu bytes)", update_partition->subtype,
           update_partition->address, update_partition->size);

  if ((uint32_t)total_len > update_partition->size) {
    ESP_LOGE(TAG, "Firmware size (%d) exceeds partition size (%lu)", total_len, update_partition->size);
    httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "Firmware too large for partition");
    return ESP_FAIL;
  }

  esp_ota_handle_t ota_handle = 0;
  esp_err_t err = esp_ota_begin(update_partition, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_begin failed: %s", esp_err_to_name(err));
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "esp_ota_begin failed");
    return ESP_FAIL;
  }

  s_ota_in_progress = true;
  if (s_status_cb) {
    s_status_cb("Starting OTA upload...");
  }

  char *buf = malloc(4096);
  if (buf == NULL) {
    ESP_LOGE(TAG, "Failed to allocate 4KB buffer for OTA receive");
    esp_ota_abort(ota_handle);
    s_ota_in_progress = false;
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Out of memory");
    return ESP_FAIL;
  }

  int remaining = total_len;
  int last_reported_pct = -1;

  while (remaining > 0) {
    int to_read = MIN(remaining, 4096);
    int recv_len = httpd_req_recv(req, buf, to_read);
    if (recv_len <= 0) {
      if (recv_len == HTTPD_SOCK_ERR_TIMEOUT) {
        continue;
      }
      ESP_LOGE(TAG, "Socket receive failed (code %d)", recv_len);
      free(buf);
      esp_ota_abort(ota_handle);
      s_ota_in_progress = false;
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Transfer interrupted");
      return ESP_FAIL;
    }

    err = esp_ota_write(ota_handle, (const void *)buf, recv_len);
    if (err != ESP_OK) {
      ESP_LOGE(TAG, "esp_ota_write failed: %s", esp_err_to_name(err));
      free(buf);
      esp_ota_abort(ota_handle);
      s_ota_in_progress = false;
      httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Flash write failed");
      return ESP_FAIL;
    }

    remaining -= recv_len;
    int pct = (int)(((int64_t)(total_len - remaining) * 100) / total_len);
    if (pct != last_reported_pct) {
      last_reported_pct = pct;
      if (s_progress_cb) {
        s_progress_cb(pct, total_len - remaining, total_len);
      }
    }
  }

  free(buf);

  err = esp_ota_end(ota_handle);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_end failed: %s", esp_err_to_name(err));
    s_ota_in_progress = false;
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "OTA validation failed");
    return ESP_FAIL;
  }

  err = esp_ota_set_boot_partition(update_partition);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "esp_ota_set_boot_partition failed: %s", esp_err_to_name(err));
    s_ota_in_progress = false;
    httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "Failed to set boot partition");
    return ESP_FAIL;
  }

  ESP_LOGI(TAG, "OTA update successfully flashed! Scheduling reboot...");
  if (s_status_cb) {
    s_status_cb("Update Complete!\nRestarting remote...");
  }

  httpd_resp_set_hdr(req, "Connection", "close");
  httpd_resp_sendstr(req, "OK");

  schedule_reboot();
  return ESP_OK;
}

static const httpd_uri_t uri_get_index = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_get_handler,
    .user_ctx = NULL,
};

static const httpd_uri_t uri_post_update = {
    .uri = "/update",
    .method = HTTP_POST,
    .handler = update_post_handler,
    .user_ctx = NULL,
};

esp_err_t local_ota_start(local_ota_progress_cb_t progress_cb, local_ota_status_cb_t status_cb) {
  if (s_http_server != NULL) {
    ESP_LOGW(TAG, "Server already running");
    return ESP_OK;
  }

  s_progress_cb = progress_cb;
  s_status_cb = status_cb;
  s_ota_in_progress = false;

  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.server_port = 80;
  config.ctrl_port = 32768;
  config.max_open_sockets = 4;
  config.stack_size = 8192;
  config.lru_purge_enable = true;

  ESP_LOGI(TAG, "Starting HTTP server on port %d...", config.server_port);
  esp_err_t err = httpd_start(&s_http_server, &config);
  if (err != ESP_OK) {
    ESP_LOGE(TAG, "Failed to start HTTP server: %s", esp_err_to_name(err));
    return err;
  }

  httpd_register_uri_handler(s_http_server, &uri_get_index);
  httpd_register_uri_handler(s_http_server, &uri_post_update);
  ESP_LOGI(TAG, "Local OTA server started successfully");
  return ESP_OK;
}

esp_err_t local_ota_stop(void) {
  if (s_http_server == NULL) {
    return ESP_OK;
  }
  ESP_LOGI(TAG, "Stopping Local OTA HTTP server...");
  esp_err_t err = httpd_stop(s_http_server);
  s_http_server = NULL;
  s_ota_in_progress = false;
  s_progress_cb = NULL;
  s_status_cb = NULL;
  return err;
}

bool local_ota_is_running(void) {
  return s_http_server != NULL;
}
