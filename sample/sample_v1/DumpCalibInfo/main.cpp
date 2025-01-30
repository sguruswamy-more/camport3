
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include "common.hpp"
#include "CalibInfoToJson.hpp"
#include "PNGTool.hpp"

//Parse Json file to CalibInfo
CalibInfoPtr JsonToCalibInfo(const std::string &jsonFile)
{
  if(jsonFile.empty()) {
    return CalibInfoPtr();
  }
  std::ifstream ifs(jsonFile);
  if (!ifs.is_open()) {
    return CalibInfoPtr();
  }
  std::stringstream buffer;
  buffer << ifs.rdbuf();
  ifs.close();
  return CalibInfoToJson::JsonStringToCalibInfo(buffer.str());
}

int testCalibInfoGet(CalibInfoPtr &calib)
{
  if (calib.get() == nullptr) {
    return -1;
  }
  //Get depth TY_CAMERA_CALIB_INFO
  calib->getDepCalib();
  //Get depth scale unit
  calib->getScaleUnit();
  //Query if depth has distortion
  calib->hasDepDistortion();
  //Query if exsist RGB TY_CAMERA_CALIB_INFO
  if(calib->hasRGB()) {
    //Get depth TY_CAMERA_CALIB_INFO
    calib->getRGBCalib();
  }
  return 0;
}

int CalibToJson(const CalibInfoPtr &calib, const std::string &jsonFile)
{
  if (calib.get() == nullptr || jsonFile.empty()){
    return -1;
  }
  std::ofstream ofs(jsonFile);
  if(!ofs.is_open()) {
    return -1;
  }
  std::string oJson = CalibInfoToJson::toJsonString(*calib);
  std::stringstream o_buffer(oJson);
  o_buffer >> ofs.rdbuf();
  ofs.close();
  return 0;
}

int SaveCalibInPNG(const CalibInfoPtr &calib, const std::string &PNGFile)
{
  if (calib.get() == nullptr || PNGFile.empty()){
    return -1;
  }
  std::string text = CalibInfoToJson::toJsonString(*calib);
  PNGInfoChunk chunk;
  chunk.EncodeChunk(text);
  return PNGTool::SavePNGChunkToFile(PNGFile, chunk);
}

//Get CalibInfo from PNG
CalibInfoPtr GetCalibFromPNG(const std::string &PNGFile)
{
  if (PNGFile.empty()){
    return CalibInfoPtr();
  }
  PNGInfoChunkPtr chunkPtr = PNGTool::ReadPNGChunkFromFile(PNGFile);
  if(chunkPtr.get() == nullptr) {
    return CalibInfoPtr();
  }
  std::string err;
  std::string text = chunkPtr->DecodeChunk(err);
  if(!err.empty()) {
    printf("PNGChunk decode err %s\n", err.c_str());
    return CalibInfoPtr();
  }
  return CalibInfoToJson::JsonStringToCalibInfo(text);
}

int getInfoFromDevice(TY_DEV_HANDLE hDevice, TY_COMPONENT_ID compID,
  bool org, TY_CAMERA_CALIB_INFO &info)
{
  TYGetStruct(hDevice, compID, TY_STRUCT_CAM_CALIB_DATA, &info, sizeof(info));
  if (!org) {
    //This will get real intrinsic to the reso set by now
    TYGetStruct(hDevice, compID, TY_STRUCT_CAM_INTRINSIC, &info.intrinsic, sizeof(info.intrinsic));
    TY_IMAGE_MODE image_mode = 0;
    TYGetEnum(hDevice, compID, TY_ENUM_IMAGE_MODE, &image_mode);
    info.intrinsicWidth = TYImageWidth(image_mode);
    info.intrinsicHeight = TYImageHeight(image_mode);
  }
  return 0;
}

/*
 * org:
 *  true, indicate use org calib intrinsic
 *  false, indicate real intrinsic to the reso set by now,
 *         MUST call after set all image mode
 */
CalibInfoPtr getCalibFromDevice(TY_DEV_HANDLE hDevice, bool org)
{
  CalibInfoPtr calibPtr(new CalibInfo());
  TY_COMPONENT_ID allComps = 0;
  TYGetComponentIDs(hDevice, &allComps);
  if(allComps & TY_COMPONENT_DEPTH_CAM) {
    TY_CAMERA_CALIB_INFO info;
    getInfoFromDevice(hDevice, TY_COMPONENT_DEPTH_CAM, org, info);
    calibPtr->setDepCalib(info);
    bool has = false;
    TYHasFeature(hDevice, TY_COMPONENT_DEPTH_CAM, TY_FLOAT_SCALE_UNIT, &has);
    if(has) {
      float scaleUnit = 1.0f;
      TYGetFloat(hDevice, TY_COMPONENT_DEPTH_CAM, TY_FLOAT_SCALE_UNIT, &scaleUnit);
      calibPtr->setScaleUnit(scaleUnit);
    }
    has = false;
    TYHasFeature(hDevice, TY_COMPONENT_DEPTH_CAM, TY_STRUCT_CAM_DISTORTION, &has);
    calibPtr->setHasDepDistortion(has);
  }
  if(allComps & TY_COMPONENT_RGB_CAM) {
    calibPtr->setHasRGB(true);
    TY_CAMERA_CALIB_INFO info;
    getInfoFromDevice(hDevice, TY_COMPONENT_RGB_CAM, org, info);
    calibPtr->setRGBCalib(info);
  }
  return calibPtr;
}

void printExtrinsics(TY_CAMERA_EXTRINSIC& extrinsics){
  printf("%f %f %f %f\n", extrinsics.data[0], extrinsics.data[1], extrinsics.data[2], extrinsics.data[3]);
  printf("%f %f %f %f\n", extrinsics.data[4], extrinsics.data[5], extrinsics.data[6], extrinsics.data[7]);
  printf("%f %f %f %f\n", extrinsics.data[8], extrinsics.data[9], extrinsics.data[10], extrinsics.data[11]);
  printf("%f %f %f %f\n", extrinsics.data[12], extrinsics.data[13], extrinsics.data[14], extrinsics.data[15]);
  return;
}

int main(int argc, char* argv[])
{
    std::string ID, IP;
    std::string outJson = "calib.json";
    TY_INTERFACE_HANDLE hIface = NULL;
    TY_DEV_HANDLE hDevice = NULL;
    int32_t cs = -1, ds = -1;
    int org = 1;

    for(int i = 1; i < argc; i++) {
        if(strcmp(argv[i], "-id") == 0){
            ID = argv[++i];
        } else if(strcmp(argv[i], "-ip") == 0) {
            IP = argv[++i];
        } else if(strcmp(argv[i], "-cs") == 0) {
            cs = atoi(argv[++i]);
        } else if(strcmp(argv[i], "-ds") == 0) {
            ds = atoi(argv[++i]);
        } else if(strcmp(argv[i], "-mode") == 0) {
           org = atoi(argv[++i]);
        } else if(strcmp(argv[i], "-out_json") == 0) {
           outJson = argv[++i];
        } else if(strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s \n\t[-h] show this help info\
            \n\t[-id <ID>] The sn of camera to dump\
            \n\t[-ip <IP>] The ip of camera to dump\
            \n\t[-cs <CS>] color reso selector in image mode list \
            \n\t[-ds <DS>] depth reso selector in image mode list \
            \n\t[-out_json <FILE>] json file to save info in\n",argv[0]);
            printf("example:\
                \n\t Dump org calibInfo: %s -id xxx -out_json org_calib.json\
                \n\t Dump realtime calibInfo: %s -id xxx -cs 1 -ds 2 -mode 0\n", argv[0], argv[0]);
            return 0;
        }
    }
    LOGD("Init lib");
    ASSERT_OK( TYInitLib() );
    TY_VERSION_INFO ver;
    ASSERT_OK( TYLibVersion(&ver) );
    LOGD("     - lib version: %d.%d.%d", ver.major, ver.minor, ver.patch);

    std::vector<TY_DEVICE_BASE_INFO> selected;
    ASSERT_OK( selectDevice(TY_INTERFACE_ALL, ID, IP, 1, selected) );
    ASSERT(selected.size() > 0);
    TY_DEVICE_BASE_INFO& selectedDev = selected[0];

    ASSERT_OK( TYOpenInterface(selectedDev.iface.id, &hIface) );
    ASSERT_OK( TYOpenDevice(hIface, selectedDev.id, &hDevice) );


    TY_COMPONENT_ID allComps;
    ASSERT_OK( TYGetComponentIDs(hDevice, &allComps) );

    if((allComps & TY_COMPONENT_RGB_CAM) && cs >= 0) {
       TY_IMAGE_MODE image_mode = 0;
       int ret = get_image_mode(hDevice, TY_COMPONENT_RGB_CAM, image_mode, cs);
       if (ret == TY_STATUS_OK) {
         TYSetEnum(hDevice, TY_COMPONENT_RGB_CAM, TY_ENUM_IMAGE_MODE, image_mode);
         LOGD("Set RGB reso %dX%d", TYImageWidth(image_mode), TYImageHeight(image_mode));
       }
    }
    if ((allComps & TY_COMPONENT_DEPTH_CAM) && ds >= 0) {
       TY_IMAGE_MODE image_mode = 0;
       int ret = get_image_mode(hDevice, TY_COMPONENT_DEPTH_CAM, image_mode, ds);
       if (ret == TY_STATUS_OK) {
         TYSetEnum(hDevice, TY_COMPONENT_DEPTH_CAM, TY_ENUM_IMAGE_MODE, image_mode);
         LOGD("Set Dep reso %dX%d", TYImageWidth(image_mode), TYImageHeight(image_mode));
       }
    }
    CalibInfoPtr calibPtr = getCalibFromDevice(hDevice, org);
    calibPtr->setSN(selectedDev.id);
    //Default timestamp is set in constructor, can update by setTimeStamp
    calibPtr->setTimeStamp(getLocalTime());
    CalibToJson(calibPtr, outJson);
    LOGD("Save calib info to Json file %s", outJson.c_str());

    // MORE TESTS
    TY_CAMERA_CALIB_INFO color_calib;
    TY_CAMERA_CALIB_INFO right_calib;
    TYGetStruct(hDevice, TY_COMPONENT_RGB_CAM_LEFT, TY_STRUCT_CAM_CALIB_DATA, &color_calib, sizeof(color_calib));
    TYGetStruct(hDevice, TY_COMPONENT_IR_CAM_RIGHT, TY_STRUCT_CAM_CALIB_DATA, &right_calib, sizeof(right_calib));
    LOGD("Color calib extrinsics from TY_STRUCT_CAM_CALIB_DATA: \n");
    printExtrinsics(color_calib.extrinsic);
    LOGD("Right calib extrinsics from TY_STRUCT_CAM_CALIB_DATA: \n");
    printExtrinsics(right_calib.extrinsic);
  
    TY_STATUS status;
    LOGD("Get color extrinsics from TY_STRUCT_EXTRINSIC_TO_DEPTH: \n");

    TY_CAMERA_EXTRINSIC color_extrinsics;
    status = TYGetStruct(hDevice, TY_COMPONENT_RGB_CAM_LEFT, TY_STRUCT_EXTRINSIC_TO_DEPTH, &color_extrinsics, sizeof(color_extrinsics));
    if (status == TY_STATUS_OK) {
      printExtrinsics(color_extrinsics);
    } else {
      LOGD("Failed to get color extrinsics from TY_STRUCT_EXTRINSIC_TO_DEPTH");
    }

    TY_CAMERA_EXTRINSIC right_extrinsics;
    LOGD("Get right extrinsics from TY_STRUCT_EXTRINSIC_TO_DEPTH: \n");
    status = TYGetStruct(hDevice, TY_COMPONENT_IR_CAM_RIGHT, TY_STRUCT_EXTRINSIC_TO_DEPTH, &right_extrinsics, sizeof(right_extrinsics));
    if (status == TY_STATUS_OK) {
      printExtrinsics(right_extrinsics);
    } else {
      LOGD("Failed to get right extrinsics from TY_STRUCT_EXTRINSIC_TO_DEPTH");
    }
    TY_CAMERA_EXTRINSIC left_extrinsics;
    LOGD("Get left extrinsics from TY_STRUCT_EXTRINSIC_TO_DEPTH: \n");
    status = TYGetStruct(hDevice, TY_COMPONENT_IR_CAM_LEFT, TY_STRUCT_EXTRINSIC_TO_DEPTH, &left_extrinsics, sizeof(left_extrinsics));
    if (status == TY_STATUS_OK) {
      printExtrinsics(left_extrinsics);
    } else {
      LOGD("Failed to get left extrinsics from TY_STRUCT_EXTRINSIC_TO_DEPTH\n");
    }


    LOGD("Get color extrinsics from TY_STRUCT_EXTRINSIC_TO_IR_LEFT: \n");
    status = TYGetStruct(hDevice, TY_COMPONENT_RGB_CAM_LEFT, TY_STRUCT_EXTRINSIC_TO_IR_LEFT, &color_extrinsics, sizeof(color_extrinsics));
    if (status == TY_STATUS_OK) {
      printExtrinsics(color_extrinsics);
    } else {
      LOGD("Failed to get color extrinsics from TY_STRUCT_EXTRINSIC_TO_IR_LEFT\n");
    }

    LOGD("Get right extrinsics from TY_STRUCT_EXTRINSIC_TO_IR_LEFT: \n");
    status = TYGetStruct(hDevice, TY_COMPONENT_IR_CAM_RIGHT, TY_STRUCT_EXTRINSIC_TO_IR_LEFT, &right_extrinsics, sizeof(right_extrinsics));
    if (status == TY_STATUS_OK) {
      printExtrinsics(right_extrinsics);
    } else {
      LOGD("Failed to get right extrinsics from TY_STRUCT_EXTRINSIC_TO_IR_LEFT\n");
    }


    ASSERT_OK( TYCloseDevice(hDevice));
    ASSERT_OK( TYCloseInterface(hIface) );
    ASSERT_OK( TYDeinitLib() );
    LOGD("Main done!");
    return 0;
}
