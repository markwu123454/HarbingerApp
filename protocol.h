#pragma once
#include <cstdint>
#include <cstring>

inline constexpr double PI = 3.14159265358979323846;

// ── Client → device ──────────────────────────────────────────────
constexpr uint8_t MSG_PING              = 0x01;
constexpr uint8_t MSG_AIM               = 0x02;
constexpr uint8_t MSG_ARM               = 0x03;
constexpr uint8_t MSG_SET_VOLTAGE       = 0x04;
constexpr uint8_t MSG_FIRE              = 0x05;
constexpr uint8_t MSG_CLEAR_CALIBRATION = 0x06;  // no payload; clears NVS cal and reboots device

// ── Device → client ──────────────────────────────────────────────
constexpr uint8_t MSG_PONG        = 0x81;
constexpr uint8_t MSG_STATE       = 0x82;
constexpr uint8_t MSG_TELEMETRY   = 0x83;
constexpr uint8_t MSG_SHOT        = 0x84;
constexpr uint8_t MSG_LOG         = 0x85;  // uint8_t level, uint8_t slen, char msg[slen]

// ── Log levels ─────────────────────────────────────────────
constexpr uint8_t LOG_INFO  = 0;
constexpr uint8_t LOG_WARN  = 1;
constexpr uint8_t LOG_ERROR = 2;

// ── Arm flag encoding ────────────────────────────────────────────
constexpr uint8_t ARM_SHIFT_MASTER = 0;
constexpr uint8_t ARM_SHIFT_TURRET = 2;
constexpr uint8_t ARM_SHIFT_GUN    = 4;
constexpr uint8_t ARM_FALSE        = 0x01;
constexpr uint8_t ARM_TRUE         = 0x02;

// ── State flag bits ──────────────────────────────────────────────
constexpr uint8_t STATE_MASTER_ARM = 0x01;
constexpr uint8_t STATE_TURRET_ARM = 0x02;
constexpr uint8_t STATE_GUN_ARM    = 0x04;
constexpr uint8_t STATE_CAL_OK     = 0x08;  // both motors have valid FOC calibration

// ── Wire-format structs ──────────────────────────────────────────────
#pragma pack(push, 1)
struct PktAim        { float heading; float elevation; };
struct PktArm        { uint8_t flags; };
struct PktSetVoltage { float voltage; };
struct PktState      { uint8_t flags; float target_v; };
struct PktTelemetry  { float heading; float elevation;
                       float motorA_vel; float motorA_acc;
                       float motorB_vel; float motorB_acc; };
struct PktShotHeader { uint32_t total_shots; uint8_t stage_count; };
struct PktShotStage  { uint32_t t_us; float v_mps; float drain_v; };
#pragma pack(pop)
