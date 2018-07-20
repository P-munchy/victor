package main

import (
	"bytes"
	"encoding/binary"
	"io/ioutil"
	"math"
	"reflect"
	"time"

	"anki/ipc"
	"anki/log"
	gw_clad "clad/gateway"
	extint "proto/external_interface"

	"github.com/golang/protobuf/proto"
	"golang.org/x/net/context"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

const faceImagePixelsPerChunk = 600

func WriteToEngine(conn ipc.Conn, msg *gw_clad.MessageExternalToRobot) (int, error) {
	var err error
	var buf bytes.Buffer
	if msg == nil {
		return -1, status.Errorf(codes.InvalidArgument, "Unable to parse request")
	}
	if err = binary.Write(&buf, binary.LittleEndian, uint16(msg.Size())); err != nil {
		return -1, status.Errorf(codes.Internal, err.Error())
	}
	if err = msg.Pack(&buf); err != nil {
		return -1, status.Errorf(codes.Internal, err.Error())
	}
	return engineSock.Write(buf.Bytes())
}

func WriteProtoToEngine(conn ipc.Conn, msg proto.Message) (int, error) {
	var err error
	var buf bytes.Buffer
	if msg == nil {
		return -1, status.Errorf(codes.InvalidArgument, "Unable to parse request")
	}
	if err = binary.Write(&buf, binary.LittleEndian, uint16(proto.Size(msg))); err != nil {
		return -1, status.Errorf(codes.Internal, err.Error())
	}
	data, err := proto.Marshal(msg)
	if err != nil {
		return -1, status.Errorf(codes.Internal, err.Error())
	}
	if err = binary.Write(&buf, binary.LittleEndian, data); err != nil {
		return -1, status.Errorf(codes.Internal, err.Error())
	}
	return protoEngineSock.Write(buf.Bytes())
}

func createChannel(tag interface{}, numChannels int) (func(), chan extint.GatewayWrapper) {
	result := make(chan extint.GatewayWrapper, numChannels)
	reflectedType := reflect.TypeOf(tag).String()
	log.Println("Listening for", reflectedType)
	engineProtoChanMap[reflectedType] = result
	return func() {
		delete(engineProtoChanMap, reflectedType)
	}, result
}

func ClearMapSetting(tag gw_clad.MessageRobotToExternalTag) {
	delete(engineChanMap, tag)
}

// TODO: we should find a way to auto-generate the equivalent of this function as part of clad or protoc
func ProtoDriveWheelsToClad(msg *extint.DriveWheelsRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithDriveWheels(&gw_clad.DriveWheels{
		LeftWheelMmps:   msg.LeftWheelMmps,
		RightWheelMmps:  msg.RightWheelMmps,
		LeftWheelMmps2:  msg.LeftWheelMmps2,
		RightWheelMmps2: msg.RightWheelMmps2,
	})
}

// TODO: we should find a way to auto-generate the equivalent of this function as part of clad or protoc
func ProtoPlayAnimationToClad(msg *extint.PlayAnimationRequest) *gw_clad.MessageExternalToRobot {
	if msg.Animation == nil {
		return nil
	}
	log.Println("Animation name:", msg.Animation.Name)
	return gw_clad.NewMessageExternalToRobotWithPlayAnimation(&gw_clad.PlayAnimation{
		NumLoops:        msg.Loops,
		AnimationName:   msg.Animation.Name,
		IgnoreBodyTrack: false,
		IgnoreHeadTrack: false,
		IgnoreLiftTrack: false,
	})
}

// TODO: we should find a way to auto-generate the equivalent of this function as part of clad or protoc
func ProtoMoveHeadToClad(msg *extint.MoveHeadRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithMoveHead(&gw_clad.MoveHead{
		SpeedRadPerSec: msg.SpeedRadPerSec,
	})
}

// TODO: we should find a way to auto-generate the equivalent of this function as part of clad or protoc
func ProtoMoveLiftToClad(msg *extint.MoveLiftRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithMoveLift(&gw_clad.MoveLift{
		SpeedRadPerSec: msg.SpeedRadPerSec,
	})
}

// TODO: we should find a way to auto-generate the equivalent of this function as part of clad or protoc
func ProtoDriveArcToClad(msg *extint.DriveArcRequest) *gw_clad.MessageExternalToRobot {
	// Bind msg.CurvatureRadiusMm which is an int32 to an int16 boundary to prevent overflow
	if msg.CurvatureRadiusMm < math.MinInt16 {
		msg.CurvatureRadiusMm = math.MinInt16
	} else if msg.CurvatureRadiusMm > math.MaxInt16 {
		msg.CurvatureRadiusMm = math.MaxInt16
	}
	return gw_clad.NewMessageExternalToRobotWithDriveArc(&gw_clad.DriveArc{
		Speed:             msg.Speed,
		Accel:             msg.Accel,
		CurvatureRadiusMm: int16(msg.CurvatureRadiusMm),
		// protobuf does not have a 16-bit integer representation, this conversion is used to satisfy the specs specified in the clad
	})
}

// TODO: we should find a way to auto-generate the equivalent of this function as part of clad or protoc
func ProtoSDKActivationToClad(msg *extint.SDKActivationRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithSDKActivationRequest(&gw_clad.SDKActivationRequest{
		Slot:   msg.Slot,
		Enable: msg.Enable,
	})
}

// TODO: we should find a way to auto-generate the equivalent of this function as part of clad or protoc
func FaceImageChunkToClad(faceData [faceImagePixelsPerChunk]uint16, pixelCount uint16, chunkIndex uint8, chunkCount uint8, durationMs uint32, interruptRunning bool) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithDisplayFaceImageRGBChunk(&gw_clad.DisplayFaceImageRGBChunk{
		FaceData:         faceData,
		NumPixels:        pixelCount,
		ChunkIndex:       chunkIndex,
		NumChunks:        chunkCount,
		DurationMs:       durationMs,
		InterruptRunning: interruptRunning,
	})
}

func ProtoAppIntentToClad(msg *extint.AppIntentRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithAppIntent(&gw_clad.AppIntent{
		Param:  msg.Param,
		Intent: msg.Intent,
	})
}

func ProtoRequestEnrolledNamesToClad(msg *extint.RequestEnrolledNamesRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithRequestEnrolledNames(&gw_clad.RequestEnrolledNames{})
}

func ProtoCancelFaceEnrollmentToClad(msg *extint.CancelFaceEnrollmentRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithCancelFaceEnrollment(&gw_clad.CancelFaceEnrollment{})
}

func ProtoUpdateEnrolledFaceByIDToClad(msg *extint.UpdateEnrolledFaceByIDRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithUpdateEnrolledFaceByID(&gw_clad.UpdateEnrolledFaceByID{
		FaceID:  msg.FaceId,
		OldName: msg.OldName,
		NewName: msg.NewName,
	})
}

func ProtoEraseEnrolledFaceByIDToClad(msg *extint.EraseEnrolledFaceByIDRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithEraseEnrolledFaceByID(&gw_clad.EraseEnrolledFaceByID{
		FaceID: msg.FaceId,
	})
}

func ProtoEraseAllEnrolledFacesToClad(msg *extint.EraseAllEnrolledFacesRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithEraseAllEnrolledFaces(&gw_clad.EraseAllEnrolledFaces{})
}

func ProtoSetFaceToEnrollToClad(msg *extint.SetFaceToEnrollRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithSetFaceToEnroll(&gw_clad.SetFaceToEnroll{
		Name:        msg.Name,
		ObservedID:  msg.ObservedId,
		SaveID:      msg.SaveId,
		SaveToRobot: msg.SaveToRobot,
		SayName:     msg.SayName,
		UseMusic:    msg.UseMusic,
	})
}

func ProtoListAnimationsToClad(msg *extint.ListAnimationsRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithRequestAvailableAnimations(&gw_clad.RequestAvailableAnimations{})
}

func ProtoEnableVisionModeToClad(msg *extint.EnableVisionModeRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithEnableVisionMode(&gw_clad.EnableVisionMode{
		Mode:   gw_clad.VisionMode(msg.Mode),
		Enable: msg.Enable,
	})
}

func ProtoPathMotionProfileToClad(msg *extint.PathMotionProfile) gw_clad.PathMotionProfile {
	return gw_clad.PathMotionProfile{
		SpeedMmps:                msg.SpeedMmps,
		AccelMmps2:               msg.AccelMmps2,
		DecelMmps2:               msg.DecelMmps2,
		PointTurnSpeedRadPerSec:  msg.PointTurnSpeedRadPerSec,
		PointTurnAccelRadPerSec2: msg.PointTurnAccelRadPerSec2,
		PointTurnDecelRadPerSec2: msg.PointTurnDecelRadPerSec2,
		DockSpeedMmps:            msg.DockSpeedMmps,
		DockAccelMmps2:           msg.DockAccelMmps2,
		DockDecelMmps2:           msg.DockDecelMmps2,
		ReverseSpeedMmps:         msg.ReverseSpeedMmps,
		IsCustom:                 msg.IsCustom,
	}
}

// TODO: we should find a way to auto-generate the equivalent of this function as part of clad or protoc
func ProtoGoToPoseToClad(msg *extint.GoToPoseRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithGotoPose(&gw_clad.GotoPose{
		XMm:        msg.XMm,
		YMm:        msg.YMm,
		Rad:        msg.Rad,
		MotionProf: ProtoPathMotionProfileToClad(msg.MotionProf),
		Level:      0,
	})
}

func ProtoDriveStraightToClad(msg *extint.DriveStraightRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithDriveStraight(&gw_clad.DriveStraight{
		SpeedMmps:           msg.SpeedMmps,
		DistMm:              msg.DistMm,
		ShouldPlayAnimation: msg.ShouldPlayAnimation,
	})
}

func ProtoTurnInPlaceToClad(msg *extint.TurnInPlaceRequest) *gw_clad.MessageExternalToRobot {
	// Bind IsAbsolute to a uint8 boundary, the clad side uses a uint8, proto side uses uint32
	if msg.IsAbsolute > math.MaxUint8 {
		msg.IsAbsolute = math.MaxUint8
	}
	return gw_clad.NewMessageExternalToRobotWithTurnInPlace(&gw_clad.TurnInPlace{
		AngleRad:        msg.AngleRad,
		SpeedRadPerSec:  msg.SpeedRadPerSec,
		AccelRadPerSec2: msg.AccelRadPerSec2,
		TolRad:          msg.TolRad,
		IsAbsolute:      uint8(msg.IsAbsolute),
	})
}

func ProtoSetHeadAngleToClad(msg *extint.SetHeadAngleRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithSetHeadAngle(&gw_clad.SetHeadAngle{
		AngleRad:          msg.AngleRad,
		MaxSpeedRadPerSec: msg.MaxSpeedRadPerSec,
		AccelRadPerSec2:   msg.AccelRadPerSec2,
		DurationSec:       msg.DurationSec,
	})
}

func ProtoSetLiftHeightToClad(msg *extint.SetLiftHeightRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithSetLiftHeight(&gw_clad.SetLiftHeight{
		HeightMm:          msg.HeightMm,
		MaxSpeedRadPerSec: msg.MaxSpeedRadPerSec,
		AccelRadPerSec2:   msg.AccelRadPerSec2,
		DurationSec:       msg.DurationSec,
	})
}

func SliceToArray(msg []uint32) [3]uint32 {
	var arr [3]uint32
	copy(arr[:], msg)
	return arr
}

func ProtoSetBackpackLEDsToClad(msg *extint.SetBackpackLEDsRequest) *gw_clad.MessageExternalToRobot {
	return gw_clad.NewMessageExternalToRobotWithSetBackpackLEDs(&gw_clad.SetBackpackLEDs{
		OnColor:               SliceToArray(msg.OnColor),
		OffColor:              SliceToArray(msg.OffColor),
		OnPeriodMs:            SliceToArray(msg.OnPeriodMs),
		OffPeriodMs:           SliceToArray(msg.OffPeriodMs),
		TransitionOnPeriodMs:  SliceToArray(msg.TransitionOnPeriodMs),
		TransitionOffPeriodMs: SliceToArray(msg.TransitionOffPeriodMs),
		Offset:                [3]int32{0, 0, 0},
	})
}

func CladCladRectToProto(msg *gw_clad.CladRect) *extint.CladRect {
	return &extint.CladRect{
		XTopLeft: msg.XTopLeft,
		YTopLeft: msg.YTopLeft,
		Width:    msg.Width,
		Height:   msg.Height,
	}
}

func CladCladPointsToProto(msg []gw_clad.CladPoint2d) []*extint.CladPoint {
	var points []*extint.CladPoint
	for _, point := range msg {
		points = append(points, &extint.CladPoint{X: point.X, Y: point.Y})
	}
	return points
}

func CladExpressionValuesToProto(msg []uint8) []uint32 {
	var expression_values []uint32
	for _, val := range msg {
		expression_values = append(expression_values, uint32(val))
	}
	return expression_values
}

func CladRobotObservedFaceToProto(msg *gw_clad.RobotObservedFace) *extint.RobotObservedFace {
	// BlinkAmount, Gaze and SmileAmount are not exposed to the SDK
	return &extint.RobotObservedFace{
		FaceId:    msg.FaceID,
		Timestamp: msg.Timestamp,
		Pose:      CladPoseToProto(&msg.Pose),
		ImgRect:   CladCladRectToProto(&msg.ImgRect),
		Name:      msg.Name,

		Expression: extint.FacialExpression(msg.Expression + 1), // protobuf enums have a 0 start value

		// Individual expression values histogram, sums to 100 (Exception: all zero if expressio: msg.
		ExpressionValues: CladExpressionValuesToProto(msg.ExpressionValues[:]),

		// Face landmarks
		LeftEye:  CladCladPointsToProto(msg.LeftEye),
		RightEye: CladCladPointsToProto(msg.RightEye),
		Nose:     CladCladPointsToProto(msg.Nose),
		Mouth:    CladCladPointsToProto(msg.Mouth),
	}
}

func CladRobotChangedObservedFaceIDToProto(msg *gw_clad.RobotChangedObservedFaceID) *extint.RobotChangedObservedFaceID {
	return &extint.RobotChangedObservedFaceID{
		OldId: msg.OldID,
		NewId: msg.NewID,
	}
}

func CladPoseToProto(msg *gw_clad.PoseStruct3d) *extint.PoseStruct {
	return &extint.PoseStruct{
		X:        msg.X,
		Y:        msg.Y,
		Z:        msg.Z,
		Q0:       msg.Q0,
		Q1:       msg.Q1,
		Q2:       msg.Q2,
		Q3:       msg.Q3,
		OriginId: msg.OriginID,
	}
}

func CladAccelDataToProto(msg *gw_clad.AccelData) *extint.AccelData {
	return &extint.AccelData{
		X: msg.X,
		Y: msg.Y,
		Z: msg.Z,
	}
}

func CladGyroDataToProto(msg *gw_clad.GyroData) *extint.GyroData {
	return &extint.GyroData{
		X: msg.X,
		Y: msg.Y,
		Z: msg.Z,
	}
}

func CladRobotStateToProto(msg *gw_clad.RobotState) *extint.RobotStateResult {
	robotState := &extint.RobotState{
		Pose:                  CladPoseToProto(&msg.Pose),
		PoseAngleRad:          msg.PoseAngleRad,
		PosePitchRad:          msg.PosePitchRad,
		LeftWheelSpeedMmps:    msg.LeftWheelSpeedMmps,
		RightWheelSpeedMmps:   msg.RightWheelSpeedMmps,
		HeadAngleRad:          msg.HeadAngleRad,
		LiftHeightMm:          msg.LiftHeightMm,
		BatteryVoltage:        msg.BatteryVoltage,
		Accel:                 CladAccelDataToProto(&msg.Accel),
		Gyro:                  CladGyroDataToProto(&msg.Gyro),
		CarryingObjectId:      msg.CarryingObjectID,
		CarryingObjectOnTopId: msg.CarryingObjectOnTopID,
		HeadTrackingObjectId:  msg.HeadTrackingObjectID,
		LocalizedToObjectId:   msg.LocalizedToObjectID,
		LastImageTimeStamp:    msg.LastImageTimeStamp,
		Status:                msg.Status,
		GameStatus:            uint32(msg.GameStatus), // protobuf does not have a uint8 representation, so cast to a uint32
	}

	return &extint.RobotStateResult{
		RobotState: robotState,
	}
}

func CladEventToProto(msg *gw_clad.Event) *extint.Event {
	switch tag := msg.Tag(); tag {
	// Event is currently unused in CLAD, but if you start
	// using it again, replace [MessageName] with your msg name
	// case gw_clad.EventTag_[MessageName]:
	// 	return &extint.Event{
	// 		EventType: &extint.Event_[MessageName]{
	// 			Clad[MessageName]ToProto(msg.Get[MessageName]()),
	// 		},
	// 	}
	case gw_clad.EventTag_INVALID:
		log.Println(tag, "tag is invalid")
		return nil
	default:
		log.Println(tag, "tag is not yet implemented")
		return nil
	}
}

func CladObjectConnectionStateToProto(msg *gw_clad.ObjectConnectionState) *extint.ObjectConnectionState {
	return &extint.ObjectConnectionState{
		ObjectId:   msg.ObjectID,
		FactoryId:  msg.FactoryID,
		ObjectType: extint.ObjectType(msg.ObjectType + 1),
		Connected:  msg.Connected,
	}
}
func CladObjectMovedToProto(msg *gw_clad.ObjectMoved) *extint.ObjectMoved {
	return &extint.ObjectMoved{
		Timestamp: msg.Timestamp,
		ObjectId:  msg.ObjectID,
	}
}
func CladObjectStoppedMovingToProto(msg *gw_clad.ObjectStoppedMoving) *extint.ObjectStoppedMoving {
	return &extint.ObjectStoppedMoving{
		Timestamp: msg.Timestamp,
		ObjectId:  msg.ObjectID,
	}
}
func CladObjectUpAxisChangedToProto(msg *gw_clad.ObjectUpAxisChanged) *extint.ObjectUpAxisChanged {
	// In clad, unknown is the final value
	// In proto, the convention is that 0 is unknown
	upAxis := extint.UpAxis_INVALID_AXIS
	if msg.UpAxis != gw_clad.UpAxis_UnknownAxis {
		upAxis = extint.UpAxis(msg.UpAxis + 1)
	}

	return &extint.ObjectUpAxisChanged{
		Timestamp: msg.Timestamp,
		ObjectId:  msg.ObjectID,
		UpAxis:    upAxis,
	}
}
func CladObjectTappedToProto(msg *gw_clad.ObjectTapped) *extint.ObjectTapped {
	return &extint.ObjectTapped{
		Timestamp: msg.Timestamp,
		ObjectId:  msg.ObjectID,
	}
}

func CladRobotObservedObjectToProto(msg *gw_clad.RobotObservedObject) *extint.RobotObservedObject {
	return &extint.RobotObservedObject{
		Timestamp:             msg.Timestamp,
		ObjectFamily:          extint.ObjectFamily(msg.ObjectFamily + 1),
		ObjectType:            extint.ObjectType(msg.ObjectType + 1),
		ObjectId:              msg.ObjectID,
		ImgRect:               CladCladRectToProto(&msg.ImgRect),
		Pose:                  CladPoseToProto(&msg.Pose),
		IsActive:              uint32(msg.IsActive),
		TopFaceOrientationRad: msg.TopFaceOrientationRad,
	}
}

func SendOnboardingContinue(in *extint.GatewayWrapper_OnboardingContinue) (*extint.OnboardingInputResponse, error) {
	f, result := createChannel(&extint.GatewayWrapper_OnboardingContinueResponse{}, 1)
	defer f()
	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: in,
	})
	if err != nil {
		return nil, err
	}
	continue_result := <-result
	return &extint.OnboardingInputResponse{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
		OneofMessageType: &extint.OnboardingInputResponse_OnboardingContinueResponse{
			OnboardingContinueResponse: continue_result.GetOnboardingContinueResponse(),
		},
	}, nil
}

func SendOnboardingSkip(in *extint.GatewayWrapper_OnboardingSkip) (*extint.OnboardingInputResponse, error) {
	log.Println("Received rpc request OnboardingSkip(", in, ")")
	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: in,
	})
	if err != nil {
		return nil, err
	}
	return &extint.OnboardingInputResponse{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func SendOnboardingConnectionComplete(in *extint.GatewayWrapper_OnboardingConnectionComplete) (*extint.OnboardingInputResponse, error) {
	log.Println("Received rpc request OnboardingConnectionComplete(", in, ")")
	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: in,
	})
	if err != nil {
		return nil, err
	}
	return &extint.OnboardingInputResponse{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func SendOnboardingSkipOnboarding(in *extint.GatewayWrapper_OnboardingSkipOnboarding) (*extint.OnboardingInputResponse, error) {
	log.Println("Received rpc request OnboardingSkipOnboarding(", in, ")")
	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: in,
	})
	if err != nil {
		return nil, err
	}
	return &extint.OnboardingInputResponse{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

// The service definition.
// This must implement all the rpc functions defined in the external_interface proto file.
type rpcService struct{}

func (m *rpcService) DriveWheels(ctx context.Context, in *extint.DriveWheelsRequest) (*extint.DriveWheelsResult, error) {
	log.Println("Received rpc request DriveWheels(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoDriveWheelsToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.DriveWheelsResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) PlayAnimation(ctx context.Context, in *extint.PlayAnimationRequest) (*extint.PlayAnimationResult, error) {
	log.Println("Received rpc request PlayAnimation(", in, ")")
	f, result := createChannel(&extint.GatewayWrapper_PlayAnimationResult{}, 1)
	defer f()

	_, err := WriteToEngine(engineSock, ProtoPlayAnimationToClad(in))
	if err != nil {
		return nil, err
	}

	setPlayAnimationResult := <-result
	response := setPlayAnimationResult.GetPlayAnimationResult()
	response.Status = &extint.ResultStatus{
		Description: "Message sent to engine",
	}
	return response, nil
}

func (m *rpcService) ListAnimations(ctx context.Context, in *extint.ListAnimationsRequest) (*extint.ListAnimationsResult, error) {
	log.Println("Received rpc request ListAnimations(", in, ")")

	// NOTE: The channels have been having issues getting clogged
	//  currently allocating a large number of channels to minimize failures
	animationAvailableResponse := make(chan RobotToExternalCladResult, 1000)
	engineChanMap[gw_clad.MessageRobotToExternalTag_AnimationAvailable] = animationAvailableResponse
	defer ClearMapSetting(gw_clad.MessageRobotToExternalTag_AnimationAvailable)

	// NOTE: The end of message has 5 channels because there was an issue
	//  where with one (or 2) channels the endOfMessage was occasionally in a similar
	//  way to the clogging on animationAvailableResponse.  This occurred despite
	//  seeing the intent to broadcast this once from the engine side.
	endOfMessageResponse := make(chan RobotToExternalCladResult, 5)
	engineChanMap[gw_clad.MessageRobotToExternalTag_EndOfMessage] = endOfMessageResponse
	defer ClearMapSetting(gw_clad.MessageRobotToExternalTag_EndOfMessage)

	_, err := WriteToEngine(engineSock, ProtoListAnimationsToClad(in))
	if err != nil {
		return nil, err
	}

	var anims []*extint.Animation

	done := false
	for done == false {
		select {
		case chanResponse := <-animationAvailableResponse:
			var newAnim = extint.Animation{
				Name: chanResponse.Message.GetAnimationAvailable().AnimName,
			}
			anims = append(anims, &newAnim)
		case chanResponse := <-endOfMessageResponse:
			if chanResponse.Message.GetEndOfMessage().MessageType == gw_clad.MessageType_AnimationAvailable {
				log.Println("Final animation list message recieved - count: ", len(anims))
				done = true
			}
		case <-time.After(5 * time.Second):
			return nil, status.Errorf(codes.DeadlineExceeded, "ListAnimations request timed out")
		}
	}

	return &extint.ListAnimationsResult{
		Status: &extint.ResultStatus{
			Description: "Available animations returned",
		},
		AnimationNames: anims,
	}, nil
}

func (m *rpcService) MoveHead(ctx context.Context, in *extint.MoveHeadRequest) (*extint.MoveHeadResult, error) {
	log.Println("Received rpc request MoveHead(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoMoveHeadToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.MoveHeadResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) MoveLift(ctx context.Context, in *extint.MoveLiftRequest) (*extint.MoveLiftResult, error) {
	log.Println("Received rpc request MoveLift(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoMoveLiftToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.MoveLiftResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) DriveArc(ctx context.Context, in *extint.DriveArcRequest) (*extint.DriveArcResult, error) {
	log.Println("Received rpc request DriveArc(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoDriveArcToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.DriveArcResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func SendFaceDataAsChunks(in *extint.DisplayFaceImageRGBRequest, chunkCount int, pixelsPerChunk int, totalPixels int) error {

	var convertedUint16Data [faceImagePixelsPerChunk]uint16

	// cycle until we run out of bytes to transfer
	for i := 0; i < chunkCount; i++ {

		pixelCount := faceImagePixelsPerChunk
		if i == chunkCount-1 {
			pixelCount = totalPixels - faceImagePixelsPerChunk*i
		}

		firstByte := (pixelsPerChunk * 2) * i
		finalByte := firstByte + (pixelCount * 2)
		slicedBinaryData := in.FaceData[firstByte:finalByte]

		for j := 0; j < pixelCount; j++ {
			uintAsBytes := slicedBinaryData[j*2 : j*2+2]
			convertedUint16Data[j] = binary.BigEndian.Uint16(uintAsBytes)
		}

		// Copy a subset of the pixels to the bytes?
		message := FaceImageChunkToClad(convertedUint16Data, uint16(pixelCount), uint8(i), uint8(chunkCount), in.DurationMs, in.InterruptRunning)

		_, err := WriteToEngine(engineSock, message)
		if err != nil {
			return err
		}
	}

	return nil
}

func (m *rpcService) DisplayFaceImageRGB(ctx context.Context, in *extint.DisplayFaceImageRGBRequest) (*extint.DisplayFaceImageRGBResult, error) {
	log.Println("Received rpc request SetOLEDToSolidColor(", in, ")")

	const totalPixels = 17664
	chunkCount := (totalPixels + faceImagePixelsPerChunk + 1) / faceImagePixelsPerChunk

	SendFaceDataAsChunks(in, chunkCount, faceImagePixelsPerChunk, totalPixels)

	return &extint.DisplayFaceImageRGBResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

// TODO: This should be handled as an event stream once RobotState is made an event
// Long running message for sending events to listening sdk users
func (c *rpcService) RobotStateStream(in *extint.RobotStateRequest, stream extint.ExternalInterface_RobotStateStreamServer) error {
	log.Println("Received rpc request RobotStateStream(", in, ")")

	stream_channel := make(chan RobotToExternalCladResult, 16)
	engineChanMap[gw_clad.MessageRobotToExternalTag_RobotState] = stream_channel
	defer ClearMapSetting(gw_clad.MessageRobotToExternalTag_RobotState)
	log.Println(engineChanMap[gw_clad.MessageRobotToExternalTag_RobotState])

	for result := range stream_channel {
		log.Printf("Got result: %+v", result)
		robotState := CladRobotStateToProto(result.Message.GetRobotState())
		log.Printf("Made RobotState: %+v", robotState)
		if err := stream.Send(robotState); err != nil {
			return err
		} else if err = stream.Context().Err(); err != nil {
			// This is the case where the user disconnects the stream
			// We should still return the err in case the user doesn't think they disconnected
			return err
		}
	}
	return nil
}

func (m *rpcService) AppIntent(ctx context.Context, in *extint.AppIntentRequest) (*extint.AppIntentResult, error) {
	log.Println("Received rpc request AppIntent(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoAppIntentToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.AppIntentResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) CancelFaceEnrollment(ctx context.Context, in *extint.CancelFaceEnrollmentRequest) (*extint.CancelFaceEnrollmentResult, error) {
	log.Println("Received rpc request CancelFaceEnrollment(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoCancelFaceEnrollmentToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.CancelFaceEnrollmentResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) RequestEnrolledNames(ctx context.Context, in *extint.RequestEnrolledNamesRequest) (*extint.RequestEnrolledNamesResult, error) {
	log.Println("Received rpc request RequestEnrolledNames(", in, ")")
	enrolledNamesResponse := make(chan RobotToExternalCladResult)
	engineChanMap[gw_clad.MessageRobotToExternalTag_EnrolledNamesResponse] = enrolledNamesResponse
	defer ClearMapSetting(gw_clad.MessageRobotToExternalTag_EnrolledNamesResponse)
	_, err := WriteToEngine(engineSock, ProtoRequestEnrolledNamesToClad(in))
	if err != nil {
		return nil, err
	}
	names := <-enrolledNamesResponse
	var faces []*extint.LoadedKnownFace
	for _, element := range names.Message.GetEnrolledNamesResponse().Faces {
		var newFace = extint.LoadedKnownFace{
			SecondsSinceFirstEnrolled: element.SecondsSinceFirstEnrolled,
			SecondsSinceLastUpdated:   element.SecondsSinceLastUpdated,
			SecondsSinceLastSeen:      element.SecondsSinceLastSeen,
			LastSeenSecondsSinceEpoch: element.LastSeenSecondsSinceEpoch,
			FaceId: element.FaceID,
			Name:   element.Name,
		}
		faces = append(faces, &newFace)
	}
	return &extint.RequestEnrolledNamesResult{
		Status: &extint.ResultStatus{
			Description: "Enrolled names returned",
		},
		Faces: faces,
	}, nil
}

// TODO Wait for response RobotRenamedEnrolledFace
func (m *rpcService) UpdateEnrolledFaceByID(ctx context.Context, in *extint.UpdateEnrolledFaceByIDRequest) (*extint.UpdateEnrolledFaceByIDResult, error) {
	log.Println("Received rpc request UpdateEnrolledFaceByID(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoUpdateEnrolledFaceByIDToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.UpdateEnrolledFaceByIDResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

// TODO Wait for response RobotRenamedEnrolledFace
func (m *rpcService) EraseEnrolledFaceByID(ctx context.Context, in *extint.EraseEnrolledFaceByIDRequest) (*extint.EraseEnrolledFaceByIDResult, error) {
	log.Println("Received rpc request EraseEnrolledFaceByID(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoEraseEnrolledFaceByIDToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.EraseEnrolledFaceByIDResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

// TODO Wait for response RobotErasedAllEnrolledFaces
func (m *rpcService) EraseAllEnrolledFaces(ctx context.Context, in *extint.EraseAllEnrolledFacesRequest) (*extint.EraseAllEnrolledFacesResult, error) {
	log.Println("Received rpc request EraseAllEnrolledFaces(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoEraseAllEnrolledFacesToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.EraseAllEnrolledFacesResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) SetFaceToEnroll(ctx context.Context, in *extint.SetFaceToEnrollRequest) (*extint.SetFaceToEnrollResult, error) {
	log.Println("Received rpc request SetFaceToEnroll(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoSetFaceToEnrollToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.SetFaceToEnrollResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

// Long running message for sending events to listening sdk users
func (c *rpcService) EventStream(in *extint.EventRequest, stream extint.ExternalInterface_EventStreamServer) error {
	log.Println("Received rpc request EventStream(", in, ")")

	f, eventsChannel := createChannel(&extint.GatewayWrapper_Event{}, 16)
	defer f()

	for result := range eventsChannel {
		log.Printf("Got result: %+v", result)
		eventResult := &extint.EventResult{
			Event: result.GetEvent(),
		}
		log.Printf("Made event: %+v", eventResult)
		if err := stream.Send(eventResult); err != nil {
			return err
		} else if err = stream.Context().Err(); err != nil {
			// This is the case where the user disconnects the stream
			// We should still return the err in case the user doesn't think they disconnected
			return err
		}
	}
	return nil
}

func (m *rpcService) SDKBehaviorActivation(ctx context.Context, in *extint.SDKActivationRequest) (*extint.SDKActivationResult, error) {
	log.Println("Received rpc request SDKBehaviorActivation(", in, ")")
	if in.Enable {
		// Request control from behavior system
		return SDKBehaviorRequestActivation(in)
	}

	return SDKBehaviorRequestDeactivation(in)
}

// Request control from behavior system.
func SDKBehaviorRequestActivation(in *extint.SDKActivationRequest) (*extint.SDKActivationResult, error) {
	// We are enabling the SDK behavior. Wait for the engine-to-game reply
	// that the SDK behavior was activated.
	sdk_activation_result := make(chan RobotToExternalCladResult)
	engineChanMap[gw_clad.MessageRobotToExternalTag_SDKActivationResult] = sdk_activation_result
	defer ClearMapSetting(gw_clad.MessageRobotToExternalTag_SDKActivationResult)

	_, err := WriteToEngine(engineSock, ProtoSDKActivationToClad(in))
	if err != nil {
		return nil, err
	}
	result := <-sdk_activation_result
	return &extint.SDKActivationResult{
		Status: &extint.ResultStatus{
			Description: "SDKActivationResult returned",
		},
		Slot:    result.Message.GetSDKActivationResult().Slot,
		Enabled: result.Message.GetSDKActivationResult().Enabled,
	}, nil
}

func SDKBehaviorRequestDeactivation(in *extint.SDKActivationRequest) (*extint.SDKActivationResult, error) {
	// Deactivate the SDK behavior. Note that we don't wait for a reply
	// so that our SDK program can exit immediately.
	_, err := WriteToEngine(engineSock, ProtoSDKActivationToClad(in))
	if err != nil {
		return nil, err
	}

	// Note: SDKActivationResult contains Slot and Enabled, which we are currently
	// ignoring when we deactivate the SDK behavior since we are not waiting for
	// the SDKActivationResult message to return.
	return &extint.SDKActivationResult{
		Status: &extint.ResultStatus{
			Description: "SDKActivationResult returned",
		},
	}, nil
}

func (m *rpcService) DriveOffCharger(ctx context.Context, in *extint.DriveOffChargerRequest) (*extint.DriveOffChargerResult, error) {
	log.Println("Received rpc request DriveOffChargerRequest(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_DriveOffChargerResult{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_DriveOffChargerRequest{
			DriveOffChargerRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	driveOffChargerResult := <-result
	response := driveOffChargerResult.GetDriveOffChargerResult()
	response.Status = &extint.ResultStatus{
		Description: "Message sent to engine",
	}
	return response, nil
}

func (m *rpcService) DriveOnCharger(ctx context.Context, in *extint.DriveOnChargerRequest) (*extint.DriveOnChargerResult, error) {
	log.Println("Received rpc request DriveOnChargerRequest(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_DriveOnChargerResult{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_DriveOnChargerRequest{
			DriveOnChargerRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	driveOnChargerResult := <-result
	response := driveOnChargerResult.GetDriveOnChargerResult()
	response.Status = &extint.ResultStatus{
		Description: "Message sent to engine",
	}
	return response, nil
}

// Example sending an int to and receiving an int from the engine
// TODO: Remove this example code once more code is converted to protobuf
func (m *rpcService) Pang(ctx context.Context, in *extint.Ping) (*extint.Pong, error) {
	log.Println("Received rpc request Ping(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_Pong{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_Ping{
			Ping: in,
		},
	})
	if err != nil {
		return nil, err
	}
	pong := <-result
	return pong.GetPong(), nil
}

// Example sending a string to and receiving a string from the engine
// TODO: Remove this example code once more code is converted to protobuf
func (m *rpcService) Bang(ctx context.Context, in *extint.Bing) (*extint.Bong, error) {
	log.Println("Received rpc request Bing(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_Bong{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_Bing{
			Bing: in,
		},
	})
	if err != nil {
		return nil, err
	}
	bong := <-result
	return bong.GetBong(), nil
}

// Request the current robot onboarding status
func (m *rpcService) GetOnboardingState(ctx context.Context, in *extint.OnboardingStateRequest) (*extint.OnboardingStateResponse, error) {
	log.Println("Received rpc request GetOnboardingState(", in, ")")
	f, result := createChannel(&extint.GatewayWrapper_OnboardingState{}, 1)
	defer f()
	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_OnboardingStateRequest{
			OnboardingStateRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	onboarding_state := <-result
	return &extint.OnboardingStateResponse{
		OnboardingState: onboarding_state.GetOnboardingState(),
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) SendOnboardingInput(ctx context.Context, in *extint.OnboardingInputRequest) (*extint.OnboardingInputResponse, error) {
	log.Println("Received rpc request OnboardingInputRequest(", in, ")")

	// oneof_message_type
	switch x := in.OneofMessageType.(type) {
	case *extint.OnboardingInputRequest_OnboardingContinue:
		return SendOnboardingContinue(&extint.GatewayWrapper_OnboardingContinue{
			OnboardingContinue: in.GetOnboardingContinue(),
		})
	case *extint.OnboardingInputRequest_OnboardingSkip:
		return SendOnboardingSkip(&extint.GatewayWrapper_OnboardingSkip{
			OnboardingSkip: in.GetOnboardingSkip(),
		})
	case *extint.OnboardingInputRequest_OnboardingConnectionComplete:
		return SendOnboardingConnectionComplete(&extint.GatewayWrapper_OnboardingConnectionComplete{
			OnboardingConnectionComplete: in.GetOnboardingConnectionComplete(),
		})
	case *extint.OnboardingInputRequest_OnboardingSkipOnboarding:
		return SendOnboardingSkipOnboarding(&extint.GatewayWrapper_OnboardingSkipOnboarding{
			OnboardingSkipOnboarding: in.GetOnboardingSkipOnboarding(),
		})
	default:
		return nil, status.Errorf(0, "OnboardingInputRequest.OneofMessageType has unexpected type %T", x)
	}
}

func (m *rpcService) PhotosInfo(ctx context.Context, in *extint.PhotosInfoRequest) (*extint.PhotosInfoResponse, error) {
	log.Println("Received rpc request PhotosInfo(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_PhotosInfoResponse{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_PhotosInfoRequest{
			PhotosInfoRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	payload := <-result
	return payload.GetPhotosInfoResponse(), nil
}

func SendImageHelper(fullpath string) ([]byte, error) {
	log.Println("Reading file at", fullpath)
	dat, err := ioutil.ReadFile(fullpath)
	if err != nil {
		log.Println("Error reading file ", fullpath)
		return nil, err
	}
	return dat, nil
}

func (m *rpcService) Photo(ctx context.Context, in *extint.PhotoRequest) (*extint.PhotoResponse, error) {
	log.Println("Received rpc request Photo(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_PhotoPathMessage{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_PhotoRequest{
			PhotoRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	payload := <-result
	if !payload.GetPhotoPathMessage().GetSuccess() {
		return &extint.PhotoResponse{
			Status: &extint.ResultStatus{
				Description: "Photo not found",
			},
			Success: false,
		}, err
	}
	imageData, err := SendImageHelper(payload.GetPhotoPathMessage().GetFullPath())
	if err != nil {
		return &extint.PhotoResponse{
			Status: &extint.ResultStatus{
				Description: "Problem reading photo file",
			},
			Success: false,
			Image:   imageData,
		}, err
	}
	return &extint.PhotoResponse{
		Status: &extint.ResultStatus{
			Description: "Photo retrieved from engine",
		},
		Success: true,
		Image:   imageData,
	}, err
}

func (m *rpcService) Thumbnail(ctx context.Context, in *extint.ThumbnailRequest) (*extint.ThumbnailResponse, error) {
	log.Println("Received rpc request Thumbnail(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_ThumbnailPathMessage{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_ThumbnailRequest{
			ThumbnailRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	payload := <-result
	if !payload.GetThumbnailPathMessage().GetSuccess() {
		return &extint.ThumbnailResponse{
			Status: &extint.ResultStatus{
				Description: "Thumbnail not found",
			},
			Success: false,
		}, err
	}
	imageData, err := SendImageHelper(payload.GetThumbnailPathMessage().GetFullPath())
	if err != nil {
		return &extint.ThumbnailResponse{
			Status: &extint.ResultStatus{
				Description: "Problem reading thumbnail file",
			},
			Success: false,
			Image:   imageData,
		}, err
	}
	return &extint.ThumbnailResponse{
		Status: &extint.ResultStatus{
			Description: "Thumbnail retrieved from engine",
		},
		Success: true,
		Image:   imageData,
	}, err
}

func (m *rpcService) DeletePhoto(ctx context.Context, in *extint.DeletePhotoRequest) (*extint.DeletePhotoResponse, error) {
	log.Println("Received rpc request DeletePhoto(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_DeletePhotoResponse{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_DeletePhotoRequest{
			DeletePhotoRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	payload := <-result
	return payload.GetDeletePhotoResponse(), nil
}

func (m *rpcService) GetLatestAttentionTransfer(ctx context.Context, in *extint.LatestAttentionTransferRequest) (*extint.LatestAttentionTransferResponse, error) {
	log.Println("Received rpc request GetLatestAttentionTransfer(", in, ")")
	f, result := createChannel(&extint.GatewayWrapper_LatestAttentionTransfer{}, 1)
	defer f()
	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_LatestAttentionTransferRequest{
			LatestAttentionTransferRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	attention_transfer := <-result
	return &extint.LatestAttentionTransferResponse{
		LatestAttentionTransfer: attention_transfer.GetLatestAttentionTransfer(),
		Status: &extint.ResultStatus{
			Description: "Response from engine",
		},
	}, nil
}

func (m *rpcService) EnableVisionMode(ctx context.Context, in *extint.EnableVisionModeRequest) (*extint.EnableVisionModeResult, error) {
	log.Println("Received rpc request EnableVisionMode(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoEnableVisionModeToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.EnableVisionModeResult{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) ConnectCube(ctx context.Context, in *extint.ConnectCubeRequest) (*extint.ConnectCubeResponse, error) {
	log.Println("Received rpc request ConnectCube(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_ConnectCubeResponse{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_ConnectCubeRequest{
			ConnectCubeRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	gatewayWrapper := <-result
	response := gatewayWrapper.GetConnectCubeResponse()
	response.Status = &extint.ResultStatus{
		Description: "Response recieved from engine",
	}
	return response, nil
}

func (m *rpcService) DisconnectCube(ctx context.Context, in *extint.DisconnectCubeRequest) (*extint.DisconnectCubeResponse, error) {
	log.Println("Received rpc request DisconnectCube(", in, ")")

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_DisconnectCubeRequest{
			DisconnectCubeRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	return &extint.DisconnectCubeResponse{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) FlashCubeLights(ctx context.Context, in *extint.FlashCubeLightsRequest) (*extint.FlashCubeLightsResponse, error) {
	log.Println("Received rpc request FlashCubeLights(", in, ")")
	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_FlashCubeLightsRequest{
			FlashCubeLightsRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	return &extint.FlashCubeLightsResponse{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) ForgetPreferredCube(ctx context.Context, in *extint.ForgetPreferredCubeRequest) (*extint.ForgetPreferredCubeResponse, error) {
	log.Println("Received rpc request ForgetPreferredCube(", in, ")")
	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_ForgetPreferredCubeRequest{
			ForgetPreferredCubeRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	return &extint.ForgetPreferredCubeResponse{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) SetPreferredCube(ctx context.Context, in *extint.SetPreferredCubeRequest) (*extint.SetPreferredCubeResponse, error) {
	log.Println("Received rpc request SetPreferredCube(", in, ")")
	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_SetPreferredCubeRequest{
			SetPreferredCubeRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	return &extint.SetPreferredCubeResponse{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) PushSettings(ctx context.Context, in *extint.PushSettingsRequest) (*extint.PushSettingsResponse, error) {
	log.Println("Received rpc request PushSettings(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_PushSettingsResponse{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_PushSettingsRequest{
			PushSettingsRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	response := <-result
	return response.GetPushSettingsResponse(), nil
}

func (m *rpcService) RobotStatusHistory(ctx context.Context, in *extint.RobotHistoryRequest) (*extint.RobotHistoryResult, error) {
	log.Println("Received rpc request RobotStatusHistory(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_RobotHistoryResult{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_RobotHistoryRequest{
			RobotHistoryRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	response := <-result
	return response.GetRobotHistoryResult(), nil
}

func (m *rpcService) PullSettings(ctx context.Context, in *extint.PullSettingsRequest) (*extint.PullSettingsResponse, error) {
	log.Println("Received rpc request PullSettings(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_PullSettingsResponse{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_PullSettingsRequest{
			PullSettingsRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	response := <-result
	return response.GetPullSettingsResponse(), nil
}

func (m *rpcService) UpdateSettings(ctx context.Context, in *extint.UpdateSettingsRequest) (*extint.UpdateSettingsResponse, error) {
	log.Println("Received rpc request UpdateSettings(", in, ")")
	tag := reflect.TypeOf(&extint.GatewayWrapper_UpdateSettingsResponse{}).String()
	if _, ok := engineProtoChanMap[tag]; ok {
		return &extint.UpdateSettingsResponse{
			Code: extint.ResultCode_ERROR_UPDATE_IN_PROGRESS,
		}, nil
	}

	f, result := createChannel(&extint.GatewayWrapper_UpdateSettingsResponse{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_UpdateSettingsRequest{
			UpdateSettingsRequest: in,
		},
	})
	if err != nil {
		return nil, err
	}
	response := <-result
	return response.GetUpdateSettingsResponse(), nil
}

func (m *rpcService) WifiScan(ctx context.Context, in *extint.WifiScanRequest) (*extint.WifiScanResponse, error) {
	log.Println("Received rpc request WifiScan(", in, ")")
	return nil, status.Errorf(codes.Unimplemented, "WifiScan not yet implemented")
}

func (m *rpcService) WifiConnect(ctx context.Context, in *extint.WifiConnectRequest) (*extint.WifiConnectResponse, error) {
	log.Println("Received rpc request WifiConnect(", in, ")")
	return nil, status.Errorf(codes.Unimplemented, "WifiConnect not yet implemented")
}

func (m *rpcService) WifiRemove(ctx context.Context, in *extint.WifiRemoveRequest) (*extint.WifiRemoveResponse, error) {
	log.Println("Received rpc request WifiRemove(", in, ")")
	return nil, status.Errorf(codes.Unimplemented, "WifiRemove not yet implemented")
}

func (m *rpcService) WifiRemoveAll(ctx context.Context, in *extint.WifiRemoveAllRequest) (*extint.WifiRemoveAllResponse, error) {
	log.Println("Received rpc request WifiRemoveAll(", in, ")")
	return nil, status.Errorf(codes.Unimplemented, "WifiRemoveAll not yet implemented")
}

// NOTE: this is the only function that won't need to check the client_token_guid header
func (m *rpcService) UserAuthentication(ctx context.Context, in *extint.UserAuthenticationRequest) (*extint.UserAuthenticationResponse, error) {
	log.Println("Received rpc request UserAuthentication(", in, ")")
	return nil, status.Errorf(codes.Unimplemented, "UserAuthentication not yet implemented")
}

func (m *rpcService) GoToPose(ctx context.Context, in *extint.GoToPoseRequest) (*extint.GoToPoseResponse, error) {
	log.Println("Received rpc request GoToPose(", in, ")")

	go_to_pose_result := make(chan RobotToExternalCladResult)
	engineChanMap[gw_clad.MessageRobotToExternalTag_RobotCompletedAction] = go_to_pose_result
	defer ClearMapSetting(gw_clad.MessageRobotToExternalTag_RobotCompletedAction)

	_, err := WriteToEngine(engineSock, ProtoGoToPoseToClad(in))
	if err != nil {
		return nil, err
	}

	result := <-go_to_pose_result
	action_result := result.Message.GetRobotCompletedAction().Result
	return &extint.GoToPoseResponse{Result: extint.ActionResult(action_result)}, nil
}

func (m *rpcService) DriveStraight(ctx context.Context, in *extint.DriveStraightRequest) (*extint.DriveStraightResponse, error) {
	log.Println("Received rpc request DriveStraight(", in, ")")
	f, result := createChannel(&extint.GatewayWrapper_DriveStraightResponse{}, 1)
	defer f()

	_, err := WriteToEngine(engineSock, ProtoDriveStraightToClad(in))
	if err != nil {
		return nil, err
	}

	driveStraightResponse := <-result
	response := driveStraightResponse.GetDriveStraightResponse()
	response.Status = &extint.ResultStatus{
		Description: "Message sent to engine",
	}
	return response, nil
}

func (m *rpcService) TurnInPlace(ctx context.Context, in *extint.TurnInPlaceRequest) (*extint.TurnInPlaceResponse, error) {
	log.Println("Received rpc request TurnInPlace(", in, ")")
	f, result := createChannel(&extint.GatewayWrapper_TurnInPlaceResponse{}, 1)
	defer f()

	_, err := WriteToEngine(engineSock, ProtoTurnInPlaceToClad(in))
	if err != nil {
		return nil, err
	}

	turnInPlaceResponse := <-result
	response := turnInPlaceResponse.GetTurnInPlaceResponse()
	response.Status = &extint.ResultStatus{
		Description: "Message sent to engine",
	}
	return response, nil
}

func (m *rpcService) SetHeadAngle(ctx context.Context, in *extint.SetHeadAngleRequest) (*extint.SetHeadAngleResponse, error) {
	log.Println("Received rpc request SetHeadAngle(", in, ")")
	f, result := createChannel(&extint.GatewayWrapper_SetHeadAngleResponse{}, 1)
	defer f()

	_, err := WriteToEngine(engineSock, ProtoSetHeadAngleToClad(in))
	if err != nil {
		return nil, err
	}

	setHeadAngleResponse := <-result
	response := setHeadAngleResponse.GetSetHeadAngleResponse()
	response.Status = &extint.ResultStatus{
		Description: "Message sent to engine",
	}
	return response, nil
}

func (m *rpcService) SetLiftHeight(ctx context.Context, in *extint.SetLiftHeightRequest) (*extint.SetLiftHeightResponse, error) {
	log.Println("Received rpc request SetLiftHeight(", in, ")")
	f, result := createChannel(&extint.GatewayWrapper_SetLiftHeightResponse{}, 1)
	defer f()

	_, err := WriteToEngine(engineSock, ProtoSetLiftHeightToClad(in))
	if err != nil {
		return nil, err
	}

	setLiftHeightResponse := <-result
	response := setLiftHeightResponse.GetSetLiftHeightResponse()
	response.Status = &extint.ResultStatus{
		Description: "Message sent to engine",
	}
	return response, nil
}

func newServer() *rpcService {
	return new(rpcService)
}

func (m *rpcService) SetBackpackLEDs(ctx context.Context, in *extint.SetBackpackLEDsRequest) (*extint.SetBackpackLEDsResponse, error) {
	log.Println("Received rpc request SetBackpackLEDs(", in, ")")
	_, err := WriteToEngine(engineSock, ProtoSetBackpackLEDsToClad(in))
	if err != nil {
		return nil, err
	}
	return &extint.SetBackpackLEDsResponse{
		Status: &extint.ResultStatus{
			Description: "Message sent to engine",
		},
	}, nil
}

func (m *rpcService) BatteryState(ctx context.Context, in *extint.BatteryStateRequest) (*extint.BatteryStateResponse, error) {
	log.Println("Received rpc request BatteryState(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_BatteryStateResponse{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_BatteryStateRequest{
			in,
		},
	})
	if err != nil {
		return nil, err
	}
	payload := <-result
	payload.GetBatteryStateResponse().Status = &extint.ResultStatus{Description: "Message sent to engine"}
	return payload.GetBatteryStateResponse(), nil
}

func (m *rpcService) VersionState(ctx context.Context, in *extint.VersionStateRequest) (*extint.VersionStateResponse, error) {
	log.Println("Received rpc request VersionState(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_VersionStateResponse{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_VersionStateRequest{
			in,
		},
	})
	if err != nil {
		return nil, err
	}
	payload := <-result
	payload.GetVersionStateResponse().Status = &extint.ResultStatus{Description: "Message sent to engine"}
	return payload.GetVersionStateResponse(), nil
}

func (m *rpcService) NetworkState(ctx context.Context, in *extint.NetworkStateRequest) (*extint.NetworkStateResponse, error) {
	log.Println("Received rpc request NetworkState(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_NetworkStateResponse{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_NetworkStateRequest{
			in,
		},
	})
	if err != nil {
		return nil, err
	}
	payload := <-result
	payload.GetNetworkStateResponse().Status = &extint.ResultStatus{Description: "Message sent to engine"}
	return payload.GetNetworkStateResponse(), nil
}

func (m *rpcService) SayText(ctx context.Context, in *extint.SayTextRequest) (*extint.SayTextResponse, error) {
	log.Println("Received rpc request SayText(", in, ")")

	f, result := createChannel(&extint.GatewayWrapper_SayTextResponse{}, 1)
	defer f()

	_, err := WriteProtoToEngine(protoEngineSock, &extint.GatewayWrapper{
		OneofMessageType: &extint.GatewayWrapper_SayTextRequest{
			in,
		},
	})
	if err != nil {
		return nil, err
	}
	payload := <-result
	sayTextResponse := payload.GetSayTextResponse()
	sayTextResponse.Status = &extint.ResultStatus{Description: "Message sent to engine"}
	return sayTextResponse, nil
}
