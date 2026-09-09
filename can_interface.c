/*
 * Copyright (c) 2026
 *
 * Hochschule Offenburg, University of Applied Sciences
 * Institute for reliable Embedded Systems
 * and Communications Electronic (ivESK)
 *
 * This file is licensed as described in the "LICENSE" file
 * included within the root folder of this work.
 */

#define _GNU_SOURCE
#include "communication_interface.h"
#include "spsec_common.h"
#include "spsec_protocol_can.h" // CanFrame
#include <arpa/inet.h>
#include <errno.h>
#include <linux/can/raw.h>
#include <linux/if.h>
#include <string.h>
#include <strings.h>
#include <sys/select.h>
#include <sys/time.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <unistd.h>

static const char *logger_name_ptr = "can_interface";

// Open, bind, and enable CAN FD on channel_ptr's socket. Cleans up on failure.
signed char can_channel_init(CommChannel *channel_ptr, const char *interface_name_ptr,
                             int bitrate, int data_bitrate) {
  channel_ptr->interface_name_ptr = strdup(interface_name_ptr);
  channel_ptr->bitrate = bitrate;
  channel_ptr->data_bitrate = data_bitrate;
  channel_ptr->socket = -1;

  if ((channel_ptr->socket = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
    LOG_ERROR(logger_name_ptr, "Failed to create CAN socket");
    goto fail;
  }

  struct ifreq ifr = {0};
  strncpy(ifr.ifr_name, interface_name_ptr, IFNAMSIZ - 1);
  ifr.ifr_name[IFNAMSIZ - 1] = '\0';
  if (ioctl(channel_ptr->socket, SIOCGIFINDEX, &ifr) < 0) {
    LOG_ERROR(logger_name_ptr, "Failed to get interface index for %s",
              interface_name_ptr);
    goto fail;
  }

  struct sockaddr_can addr = {0};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;

  if (bind(channel_ptr->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOG_ERROR(logger_name_ptr, "Failed to bind CAN socket");
    goto fail;
  }

  int enable_fd = 1;
  if (setsockopt(channel_ptr->socket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_fd,
                 sizeof(enable_fd)) < 0) {
    LOG_ERROR(logger_name_ptr, "Failed to enable CAN FD");
    goto fail;
  }

  return 0;

fail:
  if (channel_ptr->socket >= 0)
    close(channel_ptr->socket);
  free(channel_ptr->interface_name_ptr);
  channel_ptr->interface_name_ptr = NULL;
  return -1;
}

// Close the socket and free the interface name; safe on a partial init.
void can_channel_destroy(CommChannel *channel_ptr) {
  if (channel_ptr->socket >= 0)
    close(channel_ptr->socket);
  free(channel_ptr->interface_name_ptr);
  channel_ptr->interface_name_ptr = NULL;
}

// Block up to timeout ms for one CAN FD frame, wrapped as an SPsecMessage.
SPsecMessage *can_channel_receive(CommChannel *channel_ptr, int timeout) {
  if (!channel_ptr || channel_ptr->socket < 0)
    return NULL;

  // Zero-init: a short read must not leave stale stack bytes for the parser.
  struct canfd_frame frame = {0};
  struct timeval tv = {timeout / 1000, (timeout % 1000) * 1000};
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(channel_ptr->socket, &rfds);

  int ret =
      select(channel_ptr->socket + 1, &rfds, NULL, NULL, timeout ? &tv : NULL);
  if (ret <= 0) {
    return NULL;
  }

  // Read CAN frame from socket
  ssize_t nbytes = read(channel_ptr->socket, &frame, sizeof(frame));
  if (nbytes != (ssize_t)CANFD_MTU) {
    return NULL;
  }
  if (frame.len > sizeof(frame.data)) {
    return NULL;
  }

  LOG_DEBUG(logger_name_ptr, "received can_id: %08x", frame.can_id);

  AppData *ad_ptr = appdata_new(frame.can_id & CAN_EFF_MASK, frame.data, frame.len);
  if (!ad_ptr)
    return NULL;

  SPsecMessage *msg_ptr = spsecmessage_new(MSGTYPE_APP_DATA, ad_ptr);
  if (!msg_ptr) {
    appdata_free(ad_ptr);
    return NULL;
  }
  return msg_ptr;
}

signed char can_channel_receive_frame(CommChannel *channel_ptr, CanFrame *out_frame_ptr,
                                      int timeout_ms) {
  if (!channel_ptr || channel_ptr->socket < 0 || !out_frame_ptr)
    return -1;

  // Zero-init: avoid leaking stale stack bytes into the parsed frame.
  struct canfd_frame frame = {0};
  struct timeval tv = {timeout_ms / 1000, (timeout_ms % 1000) * 1000};
  fd_set rfds;
  FD_ZERO(&rfds);
  FD_SET(channel_ptr->socket, &rfds);

  int ret = select(channel_ptr->socket + 1, &rfds, NULL, NULL,
                   timeout_ms ? &tv : NULL);
  if (ret < 0) {
    if (errno == EINTR)
      return 0; // Interrupted by signal, treat as non-fatal timeout
    return -1;
  }
  if (ret == 0)
    return 0; // timeout

  // Require a full canfd_frame; classic CAN frames or bad lengths are dropped.
  ssize_t nbytes = read(channel_ptr->socket, &frame, sizeof(frame));
  if (nbytes < 0) {
    if (errno == EINTR)
      return 0;
    return -1;
  }
  if (nbytes != (ssize_t)sizeof(frame) || frame.len > sizeof(frame.data))
    return 0;

  out_frame_ptr->can_id = frame.can_id & CAN_EFF_MASK;
  out_frame_ptr->len = frame.len;
  memset(out_frame_ptr->data, 0, sizeof(out_frame_ptr->data));
  memcpy(out_frame_ptr->data, frame.data, frame.len);
  return 1;
}

// Send message_ptr over the CAN FD socket; caller keeps ownership of it.
signed char channel_send_appdata(CommChannel *channel_ptr, AppData *message_ptr) {
  if (!channel_ptr || channel_ptr->socket < 0 || !message_ptr)
    return -1;

  struct canfd_frame frame = {0};
  if (message_ptr->data_len > sizeof(frame.data)) {
    LOG_ERROR(logger_name_ptr, "AppData length %zu exceeds CAN FD max %zu",
              (size_t)message_ptr->data_len, sizeof(frame.data));
    return -1;
  }
  frame.can_id = message_ptr->address | CAN_EFF_FLAG; // Extended ID
  frame.len = (__u8)message_ptr->data_len;
  frame.flags = CANFD_BRS;
  memcpy(frame.data, message_ptr->data_ptr, message_ptr->data_len);

  char hex_preview[9];
  format_hex_string(hex_preview, sizeof(hex_preview), frame.data,
                    frame.len < 4 ? frame.len : 4);
  LOG_INFO(logger_name_ptr,
           "Sending frame: ID=%08x, Len=%d, Data=%s...",
           frame.can_id, frame.len, hex_preview);

  int retries = 0;
  ssize_t bytes_written;
  do {
    bytes_written = write(channel_ptr->socket, &frame, sizeof(frame));
    if (bytes_written < 0 && errno == EINTR) continue;
    if (bytes_written < 0 && errno == ENOBUFS && retries++ < 3) {
      usleep(1000);
      continue;
    }
    break;
  } while (1);

  if (bytes_written != sizeof(frame)) {
    if (errno == ENOBUFS) {
      LOG_WARNING(logger_name_ptr, "TX buffer overrun (ENOBUFS) after retries");
    } else {
      LOG_ERROR(logger_name_ptr, "Failed to send CAN message: %s", strerror(errno));
    }
    return -1;
  }
  return 0;
}

SPsecMessage *participant_receive_insecure_channel_message(CommChannel *channel_ptr,
                                                           int timeout) {
  return can_channel_receive(channel_ptr, timeout);
}

// Check /sys/class/net/<if>/operstate for "up"; SIOCGIFINDEX can't see link state.
signed char can_channel_check_link_alive(CommChannel *channel_ptr) {
  if (!channel_ptr || channel_ptr->socket < 0 || !channel_ptr->interface_name_ptr)
    return -1;
  char path[IFNAMSIZ + 32];
  snprintf(path, sizeof(path), "/sys/class/net/%s/operstate",
           channel_ptr->interface_name_ptr);
  FILE *f_ptr = fopen(path, "r");
  if (!f_ptr) return -1;
  char state[16] = {0};
  size_t n = fread(state, 1, sizeof(state) - 1, f_ptr);
  fclose(f_ptr);
  if (n == 0) return -1;
  char *nl_ptr = strchr(state, '\n');
  if (nl_ptr) *nl_ptr = '\0';
  if (strcmp(state, "up") == 0 || strcmp(state, "unknown") == 0) return 0;
  return -1;
}

// Re-open the socket after a link bounce (ENETDOWN/ENODEV) without restarting.
signed char can_channel_recover(CommChannel *channel_ptr) {
  if (!channel_ptr || !channel_ptr->interface_name_ptr)
    return -1;

  LOG_WARNING(logger_name_ptr, "Attempting to recover CAN channel for %s",
              channel_ptr->interface_name_ptr);

  if (channel_ptr->socket >= 0) {
    close(channel_ptr->socket);
    channel_ptr->socket = -1;
  }

  /* Re-create the socket. Bypass can_channel_destroy which also frees the
   * interface_name buffer. */
  if ((channel_ptr->socket = socket(PF_CAN, SOCK_RAW, CAN_RAW)) < 0) {
    LOG_ERROR(logger_name_ptr, "Recover: failed to create CAN socket: %s",
              strerror(errno));
    return -1;
  }

  struct ifreq ifr = {0};
  strncpy(ifr.ifr_name, channel_ptr->interface_name_ptr, IFNAMSIZ - 1);
  ifr.ifr_name[IFNAMSIZ - 1] = '\0';
  if (ioctl(channel_ptr->socket, SIOCGIFINDEX, &ifr) < 0) {
    LOG_ERROR(logger_name_ptr, "Recover: failed to get ifindex for %s: %s",
              channel_ptr->interface_name_ptr, strerror(errno));
    close(channel_ptr->socket);
    channel_ptr->socket = -1;
    return -1;
  }

  struct sockaddr_can addr = {0};
  addr.can_family = AF_CAN;
  addr.can_ifindex = ifr.ifr_ifindex;
  if (bind(channel_ptr->socket, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
    LOG_ERROR(logger_name_ptr, "Recover: failed to bind socket: %s",
              strerror(errno));
    close(channel_ptr->socket);
    channel_ptr->socket = -1;
    return -1;
  }

  int enable_fd = 1;
  if (setsockopt(channel_ptr->socket, SOL_CAN_RAW, CAN_RAW_FD_FRAMES, &enable_fd,
                 sizeof(enable_fd)) < 0) {
    LOG_ERROR(logger_name_ptr, "Recover: failed to enable CAN FD");
    close(channel_ptr->socket);
    channel_ptr->socket = -1;
    return -1;
  }

  LOG_INFO(logger_name_ptr, "CAN channel recovered for %s", channel_ptr->interface_name_ptr);
  return 0;
}