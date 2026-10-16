<!--
SPDX-FileCopyrightText: ElaadNL

SPDX-License-Identifier: Apache-2.0
-->

# Proposal Template

<style>
body {
    font-size: 10pt;
}
</style>

Version 1 | updated: 260608

A template evaluation will be based on this format. For the specification, see chapter *Template specification*.

## Glossary

- *sensors*: input values: from the appliance to the converter. These are typically values from modbus. However, they can also be hardcoded in the template or entered once on registration (userinput).
- *actuators*: output values: from the converter to the appliance. These always refer to write commands to the appliance.
- *MappingIds*: We label sensor/actuators with a mappingId; this allows the software to dynamically identify the sensor/actuator for the calculations.

### Sensors

#### Sensor mappingIds

note: Sometimes a register can hold discrete values, e.g. a status. This is interpreted with an output mapping, resulting in enums. For this, one can set apply a bitmask to a incomming message and compare the result agains a specific value. If the value matches, an enum is applied as result.

- ACTUAL_CURRENT_L1
- ACTUAL_CURRENT_L2
- ACTUAL_CURRENT_L3
- ACTUAL_CURRENT_TOTAL
  - usecase: when all current values are provided together (SUM)
- ACTUAL_VOLTAGE_L1
- ACTUAL_VOLTAGE_L2
- ACTUAL_VOLTAGE_L3
- ACTUAL_POWER_L1
- ACTUAL_POWER_L2
- ACTUAL_POWER_L3
- ACTUAL_POWER_TOTAL
  - usecase: when all power values are provided together (SUM)
- POWER_LIMIT_UPPER_START
- POWER_LIMIT_UPPER_END
- POWER_LIMIT_LOWER_START
- POWER_LIMIT_LOWER_END
- ENERGY_LIMIT_UPPER_START
- ENERGY_LIMIT_UPPER_END
- ENERGY_LIMIT_LOWER_START
- ENERGY_LIMIT_LOWER_END
- STATUS
  - outputmapping:
    - ENUM_STATUS_STATUS_OK
    - ENUM_STATUS_STATUS_ERR

##### notes on sensor mappingIds

- Here applies the rule: map as much ids as possible. These values could come from modbus. Some values (e.g. limits) could also be TEMPLATE_DEFINED or USER_INPUT. Ideally a modbus appliance shares the ACTUAL_POWER, however, some device share ACTUAL_CURRENT. In that case, one could calculate power by multiplying the current with voltage. If ACTUAL_VOLTAGE is available, this calculation becomes the most accurate.
- In case of a single phase appliance, the sensor value should be mapped to L1 (conform the S2-spec)
- a mapping ID is entered as string in the template

#### Validaty duration

With `validityDurationMs` it can be specified how long a value is valid for processing. This can be used in conjunction with `requiredSensorsBeforeEvaluation` (for actuators).

### Actuators

#### mappingIds

- SETPOINT_MAX_CURRENT_PHASE_SYMMETRIC
- SETPOINT_MAX_CURRENT_L1
- SETPOINT_MAX_CURRENT_L2
- SETPOINT_MAX_CURRENT_L3
- SETPOINT_MAX_POWER_PHASE_SYMMETRIC
- SETPOINT_MAX_POWER_L1
- SETPOINT_MAX_POWER_L2
- SETPOINT_MAX_POWER_L3
- SETPOINT_MIN_CURRENT_PHASE_SYMMETRIC
- SETPOINT_MIN_CURRENT_L1
- SETPOINT_MIN_CURRENT_L2
- SETPOINT_MIN_CURRENT_L3
- SETPOINT_MIN_POWER_PHASE_SYMMETRIC
- SETPOINT_MIN_POWER_L1
- SETPOINT_MIN_POWER_L2
- SETPOINT_MIN_POWER_L3
- HEARTBEAT
  - maybe required in some cases; a modbus could require a heartbeat signal by writing something to a specific register. However, in most cases, writing a setpoint to a register will also function as heartbeat.  

##### notes on actuator mappingIds

- `PHASE_SYMMETRIC` vs `L1/L2/L3`: use `PHASE_SYMMETRIC` when the appliance has a single control register that applies to all phases equally (e.g. most EV chargers). The value is a per-phase value (not a sum). Use `L1/L2/L3` when each phase has its own control register. This must match `control.phaseSymmetric`: when `phaseSymmetric` is true the S2 side declares ELECTRIC.POWER.3_PHASE_SYMMETRIC and instructions map onto the `PHASE_SYMMETRIC` setpoints; when false they map onto the `L1/L2/L3` setpoints.
- power -> current: when the CEM issues a power setpoint but the appliance only accepts a current setpoint, map the current variant (e.g. `SETPOINT_MAX_CURRENT_PHASE_SYMMETRIC`). The converter then computes current = power / voltage using the latest ACTUAL_VOLTAGE measurement (falling back to `control.fallbackVoltage`), so the voltage does not need to be hardcoded in the actuator `multiplier`.

Here also applies the rule: map as much ids as possible. However, it is required that the modbus appliance accepts at least one kind of setpoint limit.

### requiredSensorsBeforeEvaluation

`requiredSensorsBeforeEvaluation` accepts a list of sensors (mappingIds). Only when the CM has a valid sensor values, an actuator-command will be processed.

## TYPES

We use types to throughout the template:

- `STRING`
  - only ASCII
- `IP_ADDR`
  - regex: `^((25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)\.){3}(25[0-5]|2[0-4][0-9]|[01]?[0-9][0-9]?)$`
- `INT16`
- `UINT16`
- `INT32`
- `UINT32`
- `FLOAT32`
- `BOOL`
- `REGEX`
- `ENUM`
  - the value will be interpreted as enum

When a type is set and used for modbus reading/writing: in case of 32 bit values, two registers will be read (startAddress and the next register).

## Sources

In the template it is possible to specify where a value comes from:

- `MODBUS`
  - the value is received via Modbus
    - required params: startAddress
- `USER_INPUT`
  - the user should configure this value in the webinterface.  
    - required params: defaultValue
- `TEMPLATE_DEFINED`
  - the value is 'hardcoded' in the template.
    - required params: value
  - based on the generate the relevant input(text/number)/ or checkbox(bool)

## Constraints

The template specification is constructed with these constraints in mind:

- memory wear: check per modbus how often we can send write messages (e.g. persistent or volatile)
- endianess
- we assume the CEM is connected to a single device (even when that device is a complex combination of other sub-devices)
- check/validate firmware version
- enable instructions: some devices require a specific register write to enable control instructions, search for things like 'External Control Mode'
- heartbeat: some devices require a heartbeat signal/resubmission of instruction message; otherwise it might fall back to default operation (e.g. in combination with 'enable instruction'-command)
- limit: is this a user-limit or a system limit? ONLY USE USER-LIMIT, otherwise things might go wrong/catch fire/explode/go nuclear
- fallback settings (only in case when there is no enable instructions mechanism)
- usb-wifi-dongle: in some cases an applicance provides a modbus interface through a usb-wifi-dongle. Does this require additional items?
- combined devices (combination of pv and ess). We see them as one device, where we should be able to curtail energy flow in and - out.

## Template specification

The template consists of four sections:

- appliance: this contains general settings
- initialisationSequence: a list with modbus commands that should be run once on initialisation
- sensors: a list with sensor definitions (e.g. power measurements)
- actuators: a list with actions: commands to send to modbus (e.g. setpoint)

```c

appliance: {
 spec:{
  name: {
   source: USER_INPUT|TEMPLATE_DEFINED,
   type: STRING,
   value?,
   defaultValue?
  },
  model: {
   source: MODBUS|USER_INPUT|TEMPLATE_DEFINED,
   type: <any type>,
   value?,
   defaultValue?,
            sourceSize?: 1 | 16 | 32 | ..., // in case of modbus
  },
  serialNumber?: {
   source: MODBUS|USER_INPUT|TEMPLATE_DEFINED,
   type: <any type>,
   startAddress?: UINT16,
   value?,
   defaultValue?,
            sourceSize?: 1 | 16 | 32 | ..., // in case of modbus
  }
  expectedFirmware:{
            source: TEMPLATE_DEFINED
            type: STRING, // maybe accept regex for pattern matching?
            value
  },
  firmwareVersion: {
   source: MODBUS|USER_INPUT|TEMPLATE_DEFINED, // if USER_INPUT, the user should enter the firmware version. only continue if it matches the expectedFirmware
   type: <any type>,
   value?,
   defaultValue?,
   startAddress?: UINT16,
            sourceSize?: 1 | 16 | 32 | ..., // in case of modbus
  }, 
 },
 connection: {
  iface: ETH|RTU,
        eth?:{
            ipAddress: {
                source: USER_INPUT|TEMPLATE_DEFINED,
                type: IP_ADDR,
                value?,
                defaultValue?
            },
            gateway?: {
                source: USER_INPUT|TEMPLATE_DEFINED,
                type: IP_ADDR,
                value?,
                defaultValue?
            },
            netmask?: {
                source: USER_INPUT|TEMPLATE_DEFINED,
                type: IP_ADDR,
                value?,
                defaultValue?
            },
            port?: {
                source: USER_INPUT|TEMPLATE_DEFINED,
                type: UINT16,
                value?,
                defaultValue?
            },

        },
        rtu:{
            baudrate: {
                source: TEMPLATE_DEFINED,
                type: UINT16,
                value
            },
            modbusChannel: { // aka slaveId
                source: USER_INPUT|TEMPLATE_DEFINED,
                type: UINT16,
                value?,
                defaultValue?
            },
            parity: {
                source: TEMPLATE_DEFINED,
                type: STRING, // write as string, but we only accept 4 options (see below)
                value: '8E1' | '8O1' | '8N2' | '8N1' 
            },
            dataBits: {
                source: TEMPLATE_DEFINED,
                type: UINT16,
                value
            },
            stopBits: {
                source: TEMPLATE_DEFINED,
                type: UINT16,
                value
            },

        },

        unitId: { // aka slaveId
            source: USER_INPUT|TEMPLATE_DEFINED,
            type: UINT8,
            value?,
            defaultValue?
        },
 },
 control: {
        roles: [CONSUMER,PRODUCER,STORAGE]
  numberOfPhases: {
   source: USER_INPUT|TEMPLATE_DEFINED,
   type: UINT16,
   value?,
   defaultValue?
  },
  instructionDelayS: {
   source: TEMPLATE_DEFINED,
   type: UINT16,
   value
  },
  consequenceTypeDefer: : { 
   source: TEMPLATE_DEFINED,
   type: BOOL,
   value // if true: DEFER, if false: VANISH
  },
  phaseSymmetric?: {
   source: TEMPLATE_DEFINED,
   type: BOOL,
   value // if true: single setpoint applies to all phases (one control register, e.g. many EV chargers) -> S2 control declared as ELECTRIC.POWER.3_PHASE_SYMMETRIC, instructions map onto SETPOINT_MAX_POWER_PHASE_SYMMETRIC / SETPOINT_MIN_POWER_PHASE_SYMMETRIC.
         // if false (default): independent per-phase setpoints -> S2 control declared as ELECTRIC.POWER.L1/L2/L3, instructions map onto SETPOINT_MAX_POWER_L1/L2/L3 / SETPOINT_MIN_POWER_L1/L2/L3.
         // Independent of measurement declaration: a device may measure per phase and still be controlled symmetrically.
         // default = 0 -> for safety.
  },
  fallbackVoltage?: {
   source: TEMPLATE_DEFINED,
   type: FLOAT32,
   value // volts. Used by mapping_request_preprocessing() when an S2 power instruction must be translated to a current setpoint (template has SETPOINT_*_CURRENT_Lx / SETPOINT_*_CURRENT_PHASE_SYMMETRIC but no matching SETPOINT_*_POWER_* actuator) and no valid ACTUAL_VOLTAGE_Lx measurement is available for that phase.
         // Per-phase conversion: current_Lx = power_Lx / voltage_Lx (using the latest valid voltage measurement for that phase, else fallbackVoltage).
         // Phase-symmetric conversion: current = power / voltage_L1 (the phase-symmetric setpoint is a per-phase value that applies equally to all phases, so voltage_L1 is used by convention).
  },
 },

},
initialisationSequence: [ 
 {
  functionCode?: 'FC06' | 'FC16', // write function; defaults to empty/UNKNOWN — must be specified or the step will fail
  startAddress: UINT16,
  type: <any type>, // defaults to UINT16 if omitted
  value, // value-type is depended on type (above)
  waitMs: UINT16,
 },
 ...
 
],
sensors: [
 {
  mappingId: <any sensor mappingId>,
  source: MODBUS|USER_INPUT|TEMPLATE_DEFINED,
  functionCode?: 'FC03' | 'FC04', // read function; defaults to FC04 (input register) when omitted
  type: BOOL | (U?)INT(16/32) | FLOAT32, // the result type
  value?, // in case of TEMPLATE_DEFINED
  defaultValue?, // in case of USER_INPUT
  startAddress?: UINT16,
        sourceSize: 1 | 16 | 32 | ..., // 1 in case of discrete output, 16 in case of single register, 32 (or multiple of 16) in case of 2 or more registers. Make sure the type is in the same league. if sourceSize == 16 and type is bool, we should use a bitmask
  multiplier?: FLOAT32, // only accepted when outputmapping is not set
  bigEndian: BOOL, // only relevant if type > 16 bit
  updateIntervalMs: UINT32,
        validityDurationMs: UINT32,
        bitMask?: UINT16, // filter sections, only applicable when sourceSize = 16
        outputMapping?:[ // this is only applicable when sourceSize = 16
            { 
                value: UINT16, // when this is the result value (optionally after applying the bitmask)
                label: <any enum from sensor mappingId>
            }
        ]
 },
 ...
 
],
actuators: [
 {
  mappingId: <any actuator mappingId>,
  evaluationIntervalS: UINT16, // advised write time
  requiredSensorsBeforeEvaluation: [<any mappingId>], // only continue if these sensor have valid values (e.g. when they are set and within validityDurationMs), so we use up to date states; only mappingIds that are defined sensors are valid.
  functionCode?: 'FC06' | 'FC16', // write function; defaults to FC06 (write single register) when omitted
  type: <any type>, // the input value type
  bigEndian: BOOL, // only relevant if type > 16 bit
  multiplier: FLOAT32,
  startAddress: UINT16,
        destinationSize: 1 | 16 | 32 | ...,  // 1 in case of discrete input, 16 in case of single register, 32 (or multiple of 16) in case of 2 or more registers. Make sure the type is in the same league. if size == 16 and type is bool, we should use a bitmask
  maxStepsizeUp?: UINT16, // max allowed increment per write action (e.g. only increase 1kw per step)
  maxStepsizeDown?: UINT16, // max allowed decrement per write action (e.g. only decrease 1kw per step)
  minStepTimeMs?: UINT16, // min time between two messages, required if maxStepsizeUp or maxStepsizeDown is set
        bitMask?: UINT16, // only when a subset of bits should be modified - and only applicable if destinationSize == 16
  requiredPreSequence?: [
   {
    functionCode?: 'FC06' | 'FC16', // write function; defaults to empty/UNKNOWN — must be specified or the step will fail
    startAddress: UINT16,
    type: <any type>, // the input value type
                destinationSize: 1 | 16 | 32 | ...,  // 1 in case of discrete input, 16 in case of single register, 32 (or multiple of 16) in case of 2 or more registers. Make sure the type is in the same league. if size == 16 and type is bool, we should use a bitmask
    value, // value-type is depended on type (above)
                bitMask?: UINT16, // only when a subset of bits should be modified - and only applicable if destinationSize == 16
    waitMs: UINT16, // waitTime before next action
   },
   ...
  ],
  requiredPostSequence?: [
   {
    functionCode?: 'FC06' | 'FC16', // write function; defaults to empty/UNKNOWN — must be specified or the step will fail
    startAddress: UINT16,
    type: <any type>, // the input value type
                destinationSize: 1 | 16 | 32 | ..., // 1 in case of discrete input, 16 in case of single register, 32 (or multiple of 16) in case of 2 or more registers. Make sure the type is in the same league. if size == 16 and type is bool, we should use a bitmask
    value, // value-type is depended on type (above)
                bitMask?: UINT16, // only when a subset of bits should be modified - and only applicable if destinationSize == 16
    waitMs: UINT16, // waitTime before next action
            }
   ...
  ],

  
 },
 ...

],



```
