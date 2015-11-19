#define SERIAL_BAUD 115200

#define RESP_OK "ok"
#define RESP_ERROR "!!"
#define RESP_RESEND "rs"

#define UNITS_MM 1
#define UNITS_IN 2.54


typedef struct Command {
  char cmd;
  int cmd_num;

  float X;
  float Y;
  float Z;
  float E;
  float F;
  float I;
  float J;
  
  int P;
  int S;

  String arg;
} Command;


typedef struct PrinterState {
  boolean alive;
  boolean motors_on;
  boolean power_on;
  int fan_pwm;
  int tool;
  int line_num;
  boolean abs_pos;
  boolean e_abs_pos;
  float x;
  float y;
  float z;
  float e;
  float feed_rate;
  float tool_temp;
  float bed_temp;
  char units;
} PrinterState;


PrinterState printer = {
  .alive = true,
  .motors_on = true,
  .power_on = true,
  .fan_pwm = false,
  .tool = 0,
  .line_num = 0,
  .abs_pos = true,
  .e_abs_pos = true,
  .x = 0.0, .y = 0.0, .z = 0.0, .e = 0.0,
  .feed_rate = 0.0,
  .tool_temp = 21.0,
  .bed_temp = 21.0,
  .units = UNITS_MM
};

Command cmd;

String input = "";

/*
 * Setup serial
 */
void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.println(RESP_OK);
  input.reserve(200);
}

/*
 * Enter request response cycle for G-Code commands
 */
void loop() {
  while (Serial.available()) {
    char c = (char)Serial.read();
    input += c;
    if (c == '\n') {
      printer.line_num++;
      Serial.println(process(input));
      input = "";
    }
  }
}

String _next(String input, int* pos) {
  String tok = "";

  for (; *pos < input.length() && input[*pos] != ' '; (*pos)++) tok += input[*pos];
  
  return tok;
}

int _nextInt(String input, int* pos) {
  return _next(input, pos).toInt();
}

float _nextFloat(String input, int* pos) {
  return _next(input, pos).toFloat();
}

void clearCommand(struct Command* cmd) {
  cmd->cmd = ' ';
  cmd->cmd_num = NULL;
  cmd->X = NULL;
  cmd->Y = NULL;
  cmd->Z = NULL;
  cmd->E = NULL;
  cmd->F = NULL;
  cmd->J = NULL;
  cmd->I = NULL;
  cmd->S = NULL;
  cmd->P = NULL;
  cmd->arg = "";
}

String floatToString(float x) {
  String s = String((int)x);
  s += '.';
  x *= x < 0 ? -10 : 10;
  s += String((int)x % 10);
  x *= 10;
  s += String((int)x % 10);
  x *= 10;
  s += String((int)x % 10);
  return s;
}

/*
 * Parse a raw input string into a Command.
 */
boolean parseInput(String input, struct Command* cmd) {
  int pos = 0;
  char c;
  char arg = NULL;
  
  if (input.length() < 1) return false;

  // Ignore line numbers
  if (input[0] == 'N' || input[0] == 'n') {
    pos = input.indexOf(' ') + 1;

    if (pos < 1) return false;
  }

  cmd->cmd = input[pos];
  if (cmd->cmd >= 'a' && cmd-cmd <= 'z') cmd->cmd -= 32;
  pos++;
  
  cmd->cmd_num = _nextInt(input, &pos);

  for (; pos < input.length(); pos++) {
    c = input[pos];
    
    // Eat whitespace
    if (c == ' ') continue;

    // Uppercase argument names
    if (c >= 'a' && c <= 'z') c -= 32;

    if (arg == NULL) {
      arg = c;
    }
    else {
      switch (arg) {
        case 'X':
          cmd->X = _nextFloat(input, &pos);
          break;
        case 'Y':
          cmd->Y = _nextFloat(input, &pos);
          break;
        case 'Z':
          cmd->Z = _nextFloat(input, &pos);
          break;
        case 'E':
          cmd->E = _nextFloat(input, &pos);
          break;
        case 'F':
          cmd->F = _nextFloat(input, &pos);
          break;
        case 'I':
          cmd->I = _nextFloat(input, &pos);
          break;
        case 'J':
          cmd->J = _nextFloat(input, &pos);
          break;
        case 'P':
          cmd->P = _nextInt(input, &pos);
          break;
        case 'S':
          cmd->S = _nextInt(input, &pos);
          break;

        // Ignore checksums and comments
        case '*':
        case ';':
          return true;
        
        default:
          cmd->arg += c + input;
          break;
      }
      arg = NULL;
    }
  }

  return true;
}

/*
 * Update printer state based on G-code cmd and return response/ack 
 */
String process(String input) {
  clearCommand(&cmd);
  
  input.trim();

  if (!parseInput(input, &cmd)) {
    return RESP_OK;
  }
  
  switch (cmd.cmd) {
    case 'G':
      return process_G(&cmd);
    case 'M':
      return process_M(&cmd);
    default:
      return process_other(&cmd);
  }
}

/*
 * Process a G command.
 */
String process_G(Command* cmd) {
  switch (cmd->cmd_num) {
    case 0:   // Rapid move
    case 1:   // Normal move
    case 2:   // CW arc
    case 3:   // CCW arc
      if (printer.abs_pos) {
        if (cmd->X != NULL) printer.x = cmd->X;
        if (cmd->Y != NULL) printer.y = cmd->Y;
        if (cmd->Z != NULL) printer.z = cmd->Z;
      }
      else {
        if (cmd->X != NULL) printer.x += cmd->X;
        if (cmd->Y != NULL) printer.y += cmd->Y;
        if (cmd->Z != NULL) printer.z += cmd->Z;
      }
      if (printer.e_abs_pos) {
        if (cmd->E != NULL) printer.e = cmd->E;
      }
      else {
        if (cmd->E != NULL) printer.e += cmd->E;
      }
      if (cmd->F != NULL) printer.feed_rate = cmd->F;
      break;

    case 4:   // Dwell
    case 10:  // Retract
    case 11:  // Unretract
      break;

    case 20:  // Inch units
      printer.units = UNITS_IN;
      break;

    case 21:  // Millimeter units
      printer.units = UNITS_MM;
      break;

    case 28:  // Home
      printer.x = 0;
      printer.y = 0;
      printer.z = 0;
      break;

    case 90:  // Absolute positioning
      printer.abs_pos = true;
      break;
      
    case 91:  // Relative positioning
      printer.abs_pos = false;
      break;

    case 92:  // Set position
      if (cmd->X != NULL) printer.x = cmd->X;
      if (cmd->Y != NULL) printer.y = cmd->Y;
      if (cmd->Z != NULL) printer.z = cmd->Z;
      if (cmd->E != NULL) printer.e = cmd->E;
      break;
  }
  
  return RESP_OK;
}

/*
 * Process an M command.
 */
String process_M(Command *cmd) {
  switch (cmd->cmd_num) {
    case 0:   // Stop
    case 1:   // Sleep
      printer.alive = false;
      break;

    case 17:  // Motors on
      printer.motors_on = true;
      break;

    case 18:  // Motors off
      printer.motors_on = false;
      break;

    case 80:  // Power on
      printer.power_on = true;
      break;

    case 81:  // Power off
      printer.power_on = false;
      break;
     
    case 82:  // Extruder absolute positioning
      printer.e_abs_pos = true;
      break;

    case 83:  // Extruder relative positioning
      printer.e_abs_pos = false;
      break;

    case 104: // Set extruder temp
      if (cmd->S != NULL) printer.tool_temp = cmd->S;
      break;

    case 105: // Get temps
      return "ok T:" + String(printer.tool_temp) + " B:" + String(printer.bed_temp) +
        " @:" + floatToString(printer.tool_temp);
      break;

    case 106: // Fan on
      if (cmd->S != NULL) printer.fan_pwm = cmd->S;
      break;

    case 107: // Fan off
      printer.fan_pwm = 0;
      break;

    case 109: // Set extruder temp and wait
      if (cmd->S != NULL) printer.tool_temp = cmd->S;
      break;

    case 112: // Emergency stop
      printer.power_on = false;
      break;

    case 114: // Get current position
      return "ok C: X:" + floatToString(printer.x) +
        " Y:" + floatToString(printer.y) +
        " Z:" + floatToString(printer.z) +
        " E:" + floatToString(printer.e);

    case 115: // Get firmware description
      return "ok FIRMWARE_NAME:Marlin FIRMWARE_URL:https://github.com/dummerbd/arduino-printer-emulator PROTOCOL_VERSION:1.0 MACHINE_TYPE:Mendel EXTRUDER_COUNT:1";

    case 140: // Set bed temp
    case 190: // Set bed temp and wait
      if (cmd->S != NULL) printer.bed_temp = cmd->S;
      break;
  }
  
  return RESP_OK;
}

/*
 * Process other commands.
 */
String process_other(Command *cmd) {
  return RESP_OK;
}

