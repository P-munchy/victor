#!/usr/bin/env python3

'''
Calls specific messages on the robot, with expected results and verifies that the robot's responses match up
 - Exceptions will be raised if a response is of the wrong type, or has the wrong data
 - Exceptions will be raised if the interface defines a message that is neither on the test list or ignore list
'''

import argparse
import asyncio
import os
from pathlib import Path
import sys
import time

from google.protobuf.json_format import MessageToJson

sys.path.append(os.path.join(os.path.dirname(__file__), '../..'))
import vector

from vector.messaging import external_interface_pb2
from vector.messaging import protocol
from vector.messaging import client

interface = client.ExternalInterfaceServicer

# Both the EventStream and RobotStream should be kicked off automatically when we initialize a connection.
# while I feel it could be useful to verify these events come from the robot in response to the correct 
# stimula and that the robot state correctly represents its actual state, testing the scope of these two
# messages feels different from the other direct send->response cases and as it will probably require 
# webots state management feels improper for a rapid smoke test.
#  - nicolas 06/18/18
messages_to_ignore = [
    interface.EventStream,
    interface.RobotStateStream,
    ]

class TestResultMatches:
    _value = None
    def __init__(self, value):
        self._value = value

    def get_target_type(self):
        return type(self._value)

    def test_with(self, target):
        errors = []

        expected_type = type(self._value)
        expected_fields = [a[1] for a in self._value.ListFields()]

        target_type = type(target)
        target_fields = [a[1] for a in target.ListFields()]

        # Casting as string makes the equality check work
        if str(target_type) != str(expected_type):
            errors.append('TypeError: received output of type {0} when expecting output of type {1}'.format(target_type, expected_type))

        elif len(expected_fields) != len(target_fields):
            errors.append('TypeError: received output that appears to be a different type or contains different contents {0} than the expected output type {1}'.format(target_type, expected_type))

        else:
            # This does not perform a deep comparison, which is difficult to implement in a generic way
            for i in range(len(expected_fields)):
                if target_fields[i] != expected_fields[i]:
                    errors.append('ValueError: received output with incorrect response {0}, was expecting {1}, failure occurred with field "{2}"'.format(str(target_fields), str(expected_fields), str(target_fields[i])))
        return errors

class TestResultIsTypeWithStatusAndFieldNames:
    _expected_type = None
    _status = None
    _field_names = []
    def __init__(self, expected_type, status, field_names):
        self._expected_type = expected_type
        self._status = status
        self._field_names = field_names

    def get_target_type(self):
        return self._expected_type

    def test_with(self, target):
        errors = []

        target_type = type(target)
        target_field_names = target.DESCRIPTOR.fields_by_name.keys()

        # Casting as string makes the equality check work
        if str(target_type) != str(self._expected_type):
            errors.append('TypeError: received output of type {0} when expecting output of type {1}'.format(target_type, self._expected_type))

        elif len(self._field_names) + 1 != len(target.ListFields()):
            errors.append('TypeError: received output of type {0} that has {1} fields when {2} were expected'.format(target_type, len(target.ListFields()), len(self._field_names)+1))

        elif target.status != self._status:
            errors.append('TypeError: received output with status \'{0}\' when \'{1}\' was expected'.format(str(target.status), str(self._status)))

        else:

            for i in self._field_names:
                if not i in target_field_names:
                    errors.append('ValueError: received output with without the expected field "{0}"'.format(i))
        return errors

messages_to_test = [
    # DriveWheels message
    ( interface.DriveWheels,
        protocol.DriveWheelsRequest(left_wheel_mmps=0.0, right_wheel_mmps=0.0, left_wheel_mmps2=0.0, right_wheel_mmps2=0.0), 
        TestResultMatches(protocol.DriveWheelsResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # DriveArc message
    ( interface.DriveArc,
        protocol.DriveArcRequest(speed=0.0, accel=0.0, curvature_radius_mm=0),
        TestResultMatches(protocol.DriveArcResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # MoveHead message
    ( interface.MoveHead,
        protocol.MoveHeadRequest(speed_rad_per_sec=0.0),
        TestResultMatches(protocol.MoveHeadResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # MoveLift message
    ( interface.MoveLift,
        protocol.MoveLiftRequest(speed_rad_per_sec=0.0),
        TestResultMatches(protocol.MoveLiftResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # PlayAnimation message
    ( interface.PlayAnimation,
        protocol.PlayAnimationRequest(animation=protocol.Animation(name='anim_poked_giggle'), loops=1), 
        TestResultMatches(protocol.PlayAnimationResult(status=protocol.ResultStatus(description="Message sent to engine"),result=1)) ),

    # ListAnimations message
    ( interface.ListAnimations,
        protocol.ListAnimationsRequest(),
        TestResultIsTypeWithStatusAndFieldNames(protocol.ListAnimationsResult, protocol.ResultStatus(description="Available animations returned"), ['animation_names']) ),

    # DisplayFaceImageRGB message
    ( interface.DisplayFaceImageRGB,
        protocol.DisplayFaceImageRGBRequest(face_data=bytes(vector.color.Color(rgb=[255,0,0]).rgb565_bytepair * 17664), duration_ms=1000, interrupt_running=True), 
        TestResultMatches(protocol.DisplayFaceImageRGBResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # SDKBehaviorActivation message
    ( interface.SDKBehaviorActivation,
        protocol.SDKActivationRequest(slot=vector.robot.SDK_PRIORITY_LEVEL.SDK_HIGH_PRIORITY, enable=True), 
        TestResultMatches(protocol.SDKActivationResult(status=protocol.ResultStatus(description="SDKActivationResult returned"), slot=vector.robot.SDK_PRIORITY_LEVEL.SDK_HIGH_PRIORITY, enabled=True)) ),

    # AppIntent message
    ( interface.AppIntent,
        protocol.AppIntentRequest(intent='intent_meet_victor', param='Bobert'), 
        TestResultMatches(protocol.AppIntentResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # UpdateEnrolledFaceByID message
    ( interface.UpdateEnrolledFaceByID,
        protocol.UpdateEnrolledFaceByIDRequest(face_id=1, old_name="Bobert", new_name="Boberta"),
        TestResultMatches(protocol.UpdateEnrolledFaceByIDResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # SetFaceToEnroll message
    ( interface.SetFaceToEnroll,
        protocol.SetFaceToEnrollRequest(name="Boberta", observed_id=1, save_id=0, save_to_robot=True, say_name=True, use_music=True), 
        TestResultMatches(protocol.SetFaceToEnrollResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # CancelFaceEnrollment message
    ( interface.CancelFaceEnrollment,
        protocol.CancelFaceEnrollmentRequest(), 
        TestResultMatches(protocol.CancelFaceEnrollmentResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # EraseEnrolledFaceByID message
    ( interface.EraseEnrolledFaceByID,
        protocol.EraseEnrolledFaceByIDRequest(face_id=1), 
        TestResultMatches(protocol.EraseEnrolledFaceByIDResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # EraseAllEnrolledFaces message
    ( interface.EraseAllEnrolledFaces,
        protocol.EraseAllEnrolledFacesRequest(), 
        TestResultMatches(protocol.EraseAllEnrolledFacesResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # RequestEnrolledNames message
    ( interface.RequestEnrolledNames,
        protocol.RequestEnrolledNamesRequest(),
        TestResultMatches(protocol.RequestEnrolledNamesResult(status=protocol.ResultStatus(description="Enrolled names returned"), faces=[])) ),

    # EnableVisionMode message
    ( client.ExternalInterfaceServicer.EnableVisionMode,
        protocol.EnableVisionModeRequest(mode=protocol.VisionMode.Value("VISION_MODE_DETECTING_FACES"), enable=True),
        TestResultMatches(protocol.EnableVisionModeResult(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    ( client.ExternalInterfaceServicer.GoToPose, 
        protocol.GoToPoseRequest(x_mm=0.0, y_mm=0.0, rad=0.0, motion_prof=protocol.PathMotionProfile(speed_mmps=100.0,
                                                                                                        accel_mmps2=200.0,
                                                                                                        decel_mmps2=500.0,
                                                                                                        point_turn_speed_rad_per_sec=2.0,
                                                                                                        point_turn_accel_rad_per_sec2=10.0,
                                                                                                        point_turn_decel_rad_per_sec2=10.0,
                                                                                                        dock_speed_mmps=60.0,
                                                                                                        dock_accel_mmps2=200.0,
                                                                                                        dock_decel_mmps2=500.0,
                                                                                                        reverse_speed_mmps=80.0,
                                                                                                        is_custom=0)), 
        TestResultMatches(protocol.GoToPoseResponse(result=protocol.ActionResult.Value("ACTION_RESULT_SUCCESS"))) ),

    # DriveOffCharger message. Assuming robot starts off charger, expected result is 2 or BehaviorResults.BEHAVIOR_WONT_ACTIVATE_STATE.
    ( interface.DriveOffCharger,
        protocol.DriveOffChargerRequest(),
        TestResultMatches(protocol.DriveOffChargerResult(status=protocol.ResultStatus(description="Message sent to engine"),result=2)) ),

    # DriveOnCharger message. Expected result is 1 or BehaviorResults.BEHAVIOR_COMPLETE_STATE.
    ( interface.DriveOnCharger,
        protocol.DriveOnChargerRequest(),
        TestResultMatches(protocol.DriveOnChargerResult(status=protocol.ResultStatus(description="Message sent to engine"),result=1)) ),

    # DriveStraight message
    ( client.ExternalInterfaceServicer.DriveStraight, 
        protocol.DriveStraightRequest(speed_mmps=0.0, dist_mm=0.0, should_play_animation=False), 
        TestResultMatches(protocol.DriveStraightResponse(status=protocol.ResultStatus(description="Message sent to engine"),result=1)) ),

    # TurnInPlace message
    ( client.ExternalInterfaceServicer.TurnInPlace, 
        protocol.TurnInPlaceRequest(angle_rad=0.0, speed_rad_per_sec=0.0, accel_rad_per_sec2=0.0, tol_rad=0.0, is_absolute=False), 
        TestResultMatches(protocol.TurnInPlaceResponse(status=protocol.ResultStatus(description="Message sent to engine"),result=1)) ),
    
    # SetHeadAngle message
    ( client.ExternalInterfaceServicer.SetHeadAngle, 
        protocol.SetHeadAngleRequest(angle_rad=0.0, max_speed_rad_per_sec=0.0, accel_rad_per_sec2=0.0, duration_sec=0.0), 
        TestResultMatches(protocol.SetHeadAngleResponse(status=protocol.ResultStatus(description="Message sent to engine"),result=1)) ),
    
    # SetLiftHeight message
    ( client.ExternalInterfaceServicer.SetLiftHeight, 
        protocol.SetLiftHeightRequest(height_mm=0.0, max_speed_rad_per_sec=0.0, accel_rad_per_sec2=0.0, duration_sec=0.0), 
        TestResultMatches(protocol.SetLiftHeightResponse(status=protocol.ResultStatus(description="Message sent to engine"),result=1)) ),

    # SetBackpackLEDs message
    ( client.ExternalInterfaceServicer.SetBackpackLEDs, 
        protocol.SetBackpackLEDsRequest(on_color=[0, 0, 0], off_color=[0, 0, 0], on_period_ms=[250, 250, 250],
                                        off_period_ms=[0, 0, 0], transition_on_period_ms=[0, 0, 0], transition_off_period_ms=[0, 0, 0]), 
        TestResultMatches(protocol.SetBackpackLEDsResponse(status=protocol.ResultStatus(description="Message sent to engine"))) ),

    # NOTE: Add additional messages here
    ]

async def test_message(robot, message_name, message_src, message_input, test_class, errors):
    # The message_src is used mostly so we can easily verify that the name is supported by the servicer.  In terms of actually making the call its simpler to invoke on the robot
    message_call = getattr(robot.connection, message_name)

    print("Sending: \"{0}\"".format(MessageToJson(message_input, including_default_value_fields=True, preserving_proto_field_name=True) ))
    result = await message_call(message_input)
    print("Received: \"{0}\"".format(MessageToJson(result, including_default_value_fields=True, preserving_proto_field_name=True) ))
    
    new_errors = test_class.test_with(result)

    for i in new_errors:
        errors.append('{0}: {1}'.format(message_name, i))

async def run_message_tests(robot, future):
    warnings = []
    errors = []

    # compile a list of all functions in the interface and the input/output classes we expect them to utilize
    all_methods_in_interface = external_interface_pb2.DESCRIPTOR.services_by_name['ExternalInterface'].methods
    expected_test_list = {}
    for i in all_methods_in_interface:
        expected_test_list[i.name] = {
            'input': i.input_type,
            'output': i.output_type,
            }

    # strip out any messages that we're explicitly ignoring
    for i in messages_to_ignore:
        message_name = i.__name__
        del expected_test_list[message_name]

    # run through all listed test cases
    for i in messages_to_test:
        message_call = i[0]
        input_data = i[1]
        test_class = i[2]
        target_type = test_class.get_target_type()

        message_name = message_call.__name__
        # make sure we are expecting this message
        if message_name in expected_test_list:
            # make sure we are using the correct input class for this message
            expected_input_type = expected_test_list[message_name]['input'].name
            recieved_input_type = type(input_data).__name__
            if recieved_input_type != expected_input_type:
                errors.append('InputData: A test for a message of type {0} expects input data of the type {1}, but {2} was supplied'.format(message_name, expected_input_type, recieved_input_type))
                continue

            # make sure we are using the correct output class for this message
            expected_output_type = expected_test_list[message_name]['output'].name
            recieved_output_type = target_type.__name__
            if recieved_output_type != expected_output_type:
                errors.append('OutputData: A test for a message of type {0} expects output data of the type {1}, but {2} was supplied'.format(message_name, expected_output_type, recieved_output_type))
                continue

            print("testing " + message_name)
            await test_message(robot, message_name, message_call, input_data, test_class, errors)
            del expected_test_list[message_name]
        else:
            errors.append('NotImplemented: A test was defined for the {0} message, which is not in the interface'.format(message_name))

    # squawk if we missed any messages in the inteface
    if len(expected_test_list) > 0:
        warnings.append('NotImplemented: The following messages exist in the interface and do not have a corresponding test: {0}'.format(str(expected_test_list)))

    future.set_result({'warnings':warnings, 'errors':errors})

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("ip")
    parser.add_argument("cert_file")
    parser.add_argument("--port", default="443")
    args = parser.parse_args()
    print(args.port)

    cert = Path(args.cert_file)
    cert.resolve()

    with vector.Robot(args.ip, str(cert), port=args.port) as robot:
        print("------ beginning tests ------")

        loop = robot.loop
        future = asyncio.Future()
        robot.loop.run_until_complete(run_message_tests(robot, future))

        testResults = future.result()
        warnings = testResults['warnings']
        errors = testResults['errors']

        if len(warnings) != 0:
            print("------ warnings! ------")
            for a in warnings:
                print(a)

        print('\n')
        if len(errors) == 0:
            print("------ all tests finished successfully! ------")
            print('\n')
        else:
            print("------ tests finished with {0} errors! ------".format(len(errors)))
            for a in errors:
                print(a)
            sys.exit(1)

if __name__ == "__main__":
    main()
