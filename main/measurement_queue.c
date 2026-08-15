#include "measurement_queue.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "esp_random.h"
#include "nvs.h"
#include "nvs_flash.h"
#include "sdkconfig.h"

#define QUEUE_PARTITION "measurements"
#define QUEUE_NAMESPACE "queue"
#define QUEUE_METADATA_KEY "metadata"
#define QUEUE_MAGIC 0x42535155U
#define QUEUE_VERSION 2U

typedef struct {
  uint32_t magic;
  uint32_t version;
  uint32_t capacity;
  uint32_t head;
  uint32_t count;
  uint64_t next_sequence;
} queue_metadata_t;

typedef struct {
  uint64_t sequence;
  hive_measurement_t measurement;
} queue_record_t;

static const char *TAG = "measurement_queue";
static nvs_handle_t s_nvs;
static queue_metadata_t s_metadata;
static bool s_initialized;

static void record_key(uint32_t slot, char *key, size_t key_size) {
  snprintf(key, key_size, "m%04" PRIx32, slot);
}

static esp_err_t save_metadata(void) {
  return nvs_set_blob(s_nvs, QUEUE_METADATA_KEY, &s_metadata,
                      sizeof(s_metadata));
}

esp_err_t measurement_queue_init(void) {
  if (s_initialized) {
    return ESP_OK;
  }

  esp_err_t err = nvs_flash_init_partition(QUEUE_PARTITION);
  if (err != ESP_OK) {
    return err;
  }

  err = nvs_open_from_partition(QUEUE_PARTITION, QUEUE_NAMESPACE, NVS_READWRITE,
                                &s_nvs);
  if (err != ESP_OK) {
    return err;
  }

  size_t metadata_size = sizeof(s_metadata);
  err = nvs_get_blob(s_nvs, QUEUE_METADATA_KEY, &s_metadata, &metadata_size);
  if (err == ESP_ERR_NVS_NOT_FOUND) {
    s_metadata = (queue_metadata_t){
        .magic = QUEUE_MAGIC,
        .version = QUEUE_VERSION,
        .capacity = CONFIG_BEESCALE_MEASUREMENT_QUEUE_CAPACITY,
        .next_sequence = 1,
    };
    err = save_metadata();
    if (err == ESP_OK) {
      err = nvs_commit(s_nvs);
    }
  } else if (err == ESP_OK &&
             (metadata_size != sizeof(s_metadata) ||
              s_metadata.magic != QUEUE_MAGIC ||
              s_metadata.version != QUEUE_VERSION ||
              s_metadata.capacity < 2016 || s_metadata.capacity > 2304 ||
              s_metadata.head >= s_metadata.capacity ||
              s_metadata.count > s_metadata.capacity)) {
    err = ESP_ERR_INVALID_VERSION;
  }

  if (err != ESP_OK) {
    nvs_close(s_nvs);
    return err;
  }

  if (s_metadata.count == 0 &&
      s_metadata.capacity != CONFIG_BEESCALE_MEASUREMENT_QUEUE_CAPACITY) {
    s_metadata.capacity = CONFIG_BEESCALE_MEASUREMENT_QUEUE_CAPACITY;
    s_metadata.head = 0;
    err = save_metadata();
    if (err == ESP_OK) {
      err = nvs_commit(s_nvs);
    }
    if (err != ESP_OK) {
      nvs_close(s_nvs);
      return err;
    }
  }

  if (s_metadata.count < s_metadata.capacity) {
    const uint32_t tail =
        (s_metadata.head + s_metadata.count) % s_metadata.capacity;
    char key[8];
    record_key(tail, key, sizeof(key));
    queue_record_t orphan;
    size_t orphan_size = sizeof(orphan);
    esp_err_t orphan_err = nvs_get_blob(s_nvs, key, &orphan, &orphan_size);
    if (orphan_err == ESP_OK && orphan_size == sizeof(orphan) &&
        orphan.sequence == s_metadata.next_sequence) {
      s_metadata.count++;
      s_metadata.next_sequence++;
      err = save_metadata();
      if (err == ESP_OK) {
        err = nvs_commit(s_nvs);
      }
      if (err != ESP_OK) {
        nvs_close(s_nvs);
        return err;
      }
      ESP_LOGW(TAG, "Recovered measurement interrupted during enqueue");
    }
  }

  s_initialized = true;
  ESP_LOGI(TAG, "Queue ready with %" PRIu32 "/%" PRIu32 " measurements",
           s_metadata.count, s_metadata.capacity);
  return ESP_OK;
}

esp_err_t measurement_queue_enqueue(float weight_grams, int32_t raw_value,
                                    int64_t timestamp, bool timestamp_valid) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_metadata.count == s_metadata.capacity) {
    ESP_LOGE(TAG,
             "Queue is full; preserving %" PRIu32
             " unacknowledged measurements",
             s_metadata.count);
    return ESP_ERR_NO_MEM;
  }

  queue_record_t record = {
      .sequence = s_metadata.next_sequence,
      .measurement =
          {
              .weight_grams = weight_grams,
              .raw_value = raw_value,
              .timestamp = timestamp,
              .timestamp_valid = timestamp_valid,
          },
  };
  esp_fill_random(record.measurement.id, sizeof(record.measurement.id));

  const uint32_t slot =
      (s_metadata.head + s_metadata.count) % s_metadata.capacity;
  char key[8];
  record_key(slot, key, sizeof(key));

  const queue_metadata_t previous = s_metadata;
  esp_err_t err = nvs_set_blob(s_nvs, key, &record, sizeof(record));
  if (err == ESP_OK) {
    err = nvs_commit(s_nvs);
  }
  if (err == ESP_OK) {
    s_metadata.count++;
    s_metadata.next_sequence++;
    err = save_metadata();
  }
  if (err == ESP_OK) {
    err = nvs_commit(s_nvs);
  }
  if (err != ESP_OK) {
    s_metadata = previous;
    return err;
  }

  ESP_LOGI(TAG, "Saved measurement; queue depth is %" PRIu32, s_metadata.count);
  return ESP_OK;
}

esp_err_t measurement_queue_peek(hive_measurement_t *measurement) {
  if (!s_initialized || measurement == NULL) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_metadata.count == 0) {
    return ESP_ERR_NOT_FOUND;
  }

  char key[8];
  record_key(s_metadata.head, key, sizeof(key));
  queue_record_t record;
  size_t size = sizeof(record);
  esp_err_t err = nvs_get_blob(s_nvs, key, &record, &size);
  if (err == ESP_OK && size != sizeof(record)) {
    return ESP_ERR_INVALID_SIZE;
  }
  if (err == ESP_OK) {
    *measurement = record.measurement;
  }
  return err;
}

esp_err_t measurement_queue_pop(void) {
  if (!s_initialized) {
    return ESP_ERR_INVALID_STATE;
  }
  if (s_metadata.count == 0) {
    return ESP_ERR_NOT_FOUND;
  }

  const uint32_t acknowledged_slot = s_metadata.head;
  const queue_metadata_t previous = s_metadata;
  char key[8];
  record_key(acknowledged_slot, key, sizeof(key));

  s_metadata.head = (s_metadata.head + 1) % s_metadata.capacity;
  s_metadata.count--;
  esp_err_t err = save_metadata();
  if (err == ESP_OK) {
    err = nvs_commit(s_nvs);
  }
  if (err != ESP_OK) {
    s_metadata = previous;
    return err;
  }

  // Metadata is advanced first so a reset can only leave an orphaned record,
  // never erase a record that the queue still considers pending.
  err = nvs_erase_key(s_nvs, key);
  if (err == ESP_OK) {
    err = nvs_commit(s_nvs);
  }
  if (err != ESP_OK) {
    ESP_LOGW(TAG, "Could not reclaim acknowledged slot %" PRIu32 ": %s",
             acknowledged_slot, esp_err_to_name(err));
  }

  return ESP_OK;
}

size_t measurement_queue_count(void) {
  return s_initialized ? s_metadata.count : 0;
}

size_t measurement_queue_capacity(void) {
  return s_initialized ? s_metadata.capacity : 0;
}
