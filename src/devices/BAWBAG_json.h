#include "common_props.h"

const char* _BAWBAG_json = "{\"brand\":\"BAWBAG\",\"model\":\"THB Sensor\",\"model_id\":\"BAWBAG/001\",\"tag\":\"0107\",\"condition\":[\"index\",0,\"BAW01\",\"&\",\"manufacturerdata\",\">=\",12,\"index\",0,\"c2\"],\"properties\":{\"tempc\":{\"decoder\":[\"value_from_hex_data\",\"manufacturerdata\",2,4,true,true],\"post_proc\":[\"/\",10]},\"hum\":{\"decoder\":[\"value_from_hex_data\",\"manufacturerdata\",6,2,false,false]},\"bat\":{\"decoder\":[\"value_from_hex_data\",\"manufacturerdata\",8,4,true,false]}}}";
/*R""""(
{
   "brand":"BAWBAG",
   "model":"THB Sensor",
   "model_id":"BAWBAG/001",
   "tag":"0107",
   "condition":["index", 0, "BAW01", "&", "manufacturerdata", ">=", 12, "index", 0, "c2"],
   "properties":{
      "tempc":{
         "decoder":["value_from_hex_data", "manufacturerdata", 2, 4, true, true],
         "post_proc":["/", 10]
      },
      "hum":{
         "decoder":["value_from_hex_data", "manufacturerdata", 6, 2, false, false]
      },
      "bat":{
         "decoder":["value_from_hex_data", "manufacturerdata", 8, 4, true, false]
      }
   }
})"""";*/

const char* _BAWBAG_json_props = _common_BTH_props;
