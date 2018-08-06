# Copyright (c) 2018 Anki, Inc.

'''
Utility functions and classes for the Vector SDK
'''

# __all__ should order by constants, event classes, other classes, functions.
__all__ = ['Angle',
           'Component',
           'Pose',
           'Position',
           'Quaternion',
           'Vector3',
           'Distance',
           'Speed',
           'degrees',
           'radians',
           'angle_z_to_quaternion',
           'distance_mm',
           'distance_inches',
           'speed_mmps',
           'setup_basic_logging',
           'get_class_logger']

import argparse
import logging
import math
import os
from pathlib import Path

MODULE_LOGGER = logging.getLogger(__name__)


# TODO: Update this using the login credentials when they're available
def parse_test_args(parser: argparse.ArgumentParser = None):
    '''
    Provides the command line interface for all the tests

    :param parser: To add new arguments,
         pass an argparse parser with the new options
         already defined. Leave empty to use the defaults.
    '''
    if parser is None:
        parser = argparse.ArgumentParser()
    parser.add_argument("-n", "--name", nargs='?', default=os.environ.get('VECTOR_ROBOT_NAME', None))
    parser.add_argument("-i", "--ip", nargs='?', default=os.environ.get('VECTOR_ROBOT_IP', None))
    parser.add_argument("-c", "--cert_file", nargs='?', default=os.environ.get('VECTOR_ROBOT_CERT', None))
    parser.add_argument("--port", nargs='?', default="443")
    args = parser.parse_args()
    if args.port == "8443":
        args.name = os.environ.get('VECTOR_ROBOT_NAME_MAC', args.name)
        args.ip = os.environ.get('VECTOR_ROBOT_IP_MAC', args.ip)
        args.cert_file = os.environ.get('VECTOR_ROBOT_CERT_MAC', args.cert_file)

    if args.name is None or args.ip is None or args.cert_file is None:
        parser.error('the following arguments are required: name, ip, cert_file '
                     'or they may be set with the environment variables: '
                     'VECTOR_ROBOT_NAME, VECTOR_ROBOT_IP, VECTOR_ROBOT_CERT '
                     'respectively')

    cert = Path(args.cert_file)
    args.cert = cert.resolve()
    return args


def setup_basic_logging(custom_handler: logging.Handler = None,
                        general_log_level: str = None,
                        target: object = None):
    '''Helper to perform basic setup of the Python logging machinery.

    :param custom_handler: provide an external logger for custom logging locations
    :param general_log_level: 'DEBUG', 'INFO', 'WARN', 'ERROR' or an equivalent
            constant from the :mod:`logging` module.  If None then a
            value will be read from the VECTOR_LOG_LEVEL environment variable.
    :param target: The stream to send the log data to; defaults to stderr
    '''
    if general_log_level is None:
        general_log_level = os.environ.get('VICTOR_LOG_LEVEL', logging.DEBUG)

    handler = custom_handler
    if handler is None:
        handler = logging.StreamHandler(stream=target)
        formatter = logging.Formatter('%(asctime)s %(name)-12s %(levelname)-8s %(message)s')
        handler.setFormatter(formatter)

    vector_logger = logging.getLogger('vector')
    if not vector_logger.handlers:
        vector_logger.addHandler(handler)
        vector_logger.setLevel(general_log_level)


def get_class_logger(module: str, obj: object) -> logging.Logger:
    '''Helper to create logger for a given class (and module)

    :param module: The name of the module to which the object belongs.
    :param obj: the object that owns the logger.
    '''
    return logging.getLogger(".".join([module, type(obj).__name__]))


def angle_z_to_quaternion(angle_z):
    '''This function converts an angle in the z axis (Euler angle z component) to a quaternion.

    Args:
        angle_z (:class:`vector.util.Angle`): The z axis angle.

    Returns:
        q0, q1, q2, q3 (float, float, float, float): A tuple with all the members
            of a quaternion defined by angle_z.
    '''

    # Define the quaternion to be converted from a Euler angle (x,y,z) of 0,0,angle_z
    # These equations have their original equations above, and simplified implemented
    # q0 = cos(x/2)*cos(y/2)*cos(z/2) + sin(x/2)*sin(y/2)*sin(z/2)
    q0 = math.cos(angle_z.radians / 2)
    # q1 = sin(x/2)*cos(y/2)*cos(z/2) - cos(x/2)*sin(y/2)*sin(z/2)
    q1 = 0
    # q2 = cos(x/2)*sin(y/2)*cos(z/2) + sin(x/2)*cos(y/2)*sin(z/2)
    q2 = 0
    # q3 = cos(x/2)*cos(y/2)*sin(z/2) - sin(x/2)*sin(y/2)*cos(z/2)
    q3 = math.sin(angle_z.radians / 2)
    return q0, q1, q2, q3


class Vector3:
    '''Represents a 3D Vector (type/units aren't specified)

    :param x: X component
    :param y: Y component
    :param z: Z component
    '''

    __slots__ = ('_x', '_y', '_z')

    def __init__(self, x: float, y: float, z: float):
        self._x = x
        self._y = y
        self._z = z

    @property
    def x(self):
        return self._x

    @property
    def y(self):
        return self._y

    @property
    def z(self):
        return self._z

    @property
    def x_y_z(self):
        '''tuple (float, float, float): The X, Y, Z elements of the Vector3 (x,y,z)'''
        return self._x, self._y, self._z

    def __repr__(self):
        return f"<{self.__class__.__name__} x: {self.x:.2} y: {self.y:.2} z: {self.z:.2}>"


class Angle:
    '''Represents an angle.

    Use the :func:`degrees` or :func:`radians` convenience methods to generate
    an Angle instance.

    :param radians: The number of radians the angle should represent
        (cannot be combined with ``degrees``)
    :param degrees: The number of degress the angle should represent
        (cannot be combined with ``radians``)
    '''

    __slots__ = ('_radians')

    def __init__(self, radians: float = None, degrees: float = None):  # pylint: disable=redefined-outer-name
        if radians is None and degrees is None:
            raise ValueError("Expected either the degrees or radians keyword argument")
        if radians and degrees:
            raise ValueError("Expected either the degrees or radians keyword argument, not both")

        if degrees is not None:
            radians = degrees * math.pi / 180
        self._radians = float(radians)

    @property
    def radians(self):  # pylint: disable=redefined-outer-name
        '''float: The angle in radians.'''
        return self._radians

    @property
    def degrees(self):  # pylint: disable=redefined-outer-name
        '''float: The angle in degrees.'''
        return self._radians / math.pi * 180

    def __repr__(self):
        return f"<{self.__class__.__name__} Radians: {self.radians:.2} Degrees: {self.degrees:.2}>"

    def __add__(self, other):
        if not isinstance(other, Angle):
            raise TypeError("Unsupported type for + expected Angle")
        return Angle(radians=(self.radians + other.radians))

    def __sub__(self, other):
        if not isinstance(other, Angle):
            raise TypeError("Unsupported type for - expected Angle")
        return Angle(radians=(self.radians - other.radians))

    def __mul__(self, other):
        if not isinstance(other, (int, float)):
            raise TypeError("Unsupported type for * expected number")
        return Angle(radians=(self.radians * other))


def degrees(degrees: float):  # pylint: disable=redefined-outer-name
    '''Returns an :class:`vector.util.Angle` instance set to the specified number of degrees.'''
    return Angle(degrees=degrees)


def radians(radians: float):  # pylint: disable=redefined-outer-name
    '''Returns an :class:`vector.util.Angle` instance set to the specified number of radians.'''
    return Angle(radians=radians)


class Quaternion:
    '''Represents the rotation of an object in the world.'''

    __slots__ = ('_q0', '_q1', '_q2', '_q3')

    def __init__(self, q0=None, q1=None, q2=None, q3=None, angle_z=None):
        is_quaternion = q0 is not None and q1 is not None and q2 is not None and q3 is not None

        if not is_quaternion and angle_z is None:
            raise ValueError("Expected either the q0 q1 q2 and q3 or angle_z keyword arguments")
        if is_quaternion and angle_z:
            raise ValueError("Expected either the q0 q1 q2 and q3 or angle_z keyword argument,"
                             "not both")
        if angle_z is not None:
            if not isinstance(angle_z, Angle):
                raise TypeError("Unsupported type for angle_z expected Angle")
            q0, q1, q2, q3 = angle_z_to_quaternion(angle_z)

        self._q0 = q0
        self._q1 = q1
        self._q2 = q2
        self._q3 = q3

    @property
    def q0(self):
        return self._q0

    @property
    def q1(self):
        return self._q1

    @property
    def q2(self):
        return self._q2

    @property
    def q3(self):
        return self._q3

    @property
    def angle_z(self):
        '''class:`Angle`: The z Euler component of the object's rotation.

        Defined as the rotation in the z axis.
        '''
        q0, q1, q2, q3 = self.q0_q1_q2_q3
        return Angle(radians=math.atan2(2 * (q1 * q2 + q0 * q3), 1 - 2 * (q2**2 + q3**2)))

    @property
    def q0_q1_q2_q3(self):
        '''tuple of float: Contains all elements of the quaternion (q0,q1,q2,q3)'''
        return self._q0, self._q1, self._q2, self._q3

    def __repr__(self):
        return (f"<{self.__class__.__name__} q0: {self.q0:.2} q1: {self.q1:.2}"
                f" q2: {self.q2:.2} q3: {self.q3:.2} {self.angle_z}>")


class Position(Vector3):
    '''Represents the position of an object in the world.

    A position consists of its x, y and z values in millimeters.

    :param x: X position in millimeters
    :param y: Y position in millimeters
    :param z: Z position in millimeters
    '''
    __slots__ = ()


class Pose:
    '''Represents the current pose (position and orientation) of vector'''

    __slots__ = ('_position', '_rotation', '_origin_id')

    def __init__(self, x, y, z, q0=None, q1=None, q2=None, q3=None,
                 angle_z=None, origin_id=-1):
        self._position = Position(x, y, z)
        self._rotation = Quaternion(q0, q1, q2, q3, angle_z)
        self._origin_id = origin_id

    @property
    def position(self):
        return self._position

    @property
    def rotation(self):
        return self._rotation

    @property
    def origin_id(self):
        return self._origin_id

    def __repr__(self):
        return (f"<{self.__class__.__name__}: {self._position}"
                f" {self._rotation} <Origin Id: {self._origin_id}>>")

    def define_pose_relative_this(self, new_pose):
        '''Creates a new pose such that new_pose's origin is now at the location of this pose.

        Args:
            new_pose (:class:`vector.util.Pose`): The pose which origin is being changed.

        Returns:
            A :class:`vector.util.pose` object for which the origin was this pose's origin.
        '''

        if not isinstance(new_pose, Pose):
            raise TypeError("Unsupported type for new_origin, must be of type Pose")
        x, y, z = self.position.x_y_z
        angle_z = self.rotation.angle_z
        new_x, new_y, new_z = new_pose.position.x_y_z
        new_angle_z = new_pose.rotation.angle_z

        cos_angle = math.cos(angle_z.radians)
        sin_angle = math.sin(angle_z.radians)
        res_x = x + (cos_angle * new_x) - (sin_angle * new_y)
        res_y = y + (sin_angle * new_x) + (cos_angle * new_y)
        res_z = z + new_z
        res_angle = angle_z + new_angle_z
        return Pose(res_x,
                    res_y,
                    res_z,
                    angle_z=res_angle,
                    origin_id=self._origin_id)


class ImageRect:
    '''Image co-ordinates and size'''

    __slots__ = ('_x_top_left', '_y_top_left', '_width', '_height')

    def __init__(self, x_top_left, y_top_left, width, height):
        self._x_top_left = x_top_left
        self._y_top_left = y_top_left
        self._width = width
        self._height = height

    @property
    def x_top_left(self):
        return self._x_top_left

    @property
    def y_top_left(self):
        return self._y_top_left

    @property
    def width(self):
        return self._width

    @property
    def height(self):
        return self._height


class Distance:
    '''Represents a distance.

    The class allows distances to be returned in either millimeters or inches.

    Use the :func:`distance_inches` or :func:`distance_mm` convenience methods to generate
    a Distance instance.

    Args:
        distance_mm (float): The number of millimeters the distance should
            represent (cannot be combined with ``distance_inches``).
        distance_inches (float): The number of inches the distance should
            represent (cannot be combined with ``distance_mm``).
    '''

    __slots__ = ('_distance_mm')

    def __init__(self, distance_mm=None, distance_inches=None):  # pylint: disable=redefined-outer-name
        if distance_mm is None and distance_inches is None:
            raise ValueError("Expected either the distance_mm or distance_inches keyword argument")
        if distance_mm and distance_inches:
            raise ValueError("Expected either the distance_mm or distance_inches keyword argument, not both")

        if distance_inches is not None:
            distance_mm = distance_inches * 25.4
        self._distance_mm = distance_mm

    def __repr__(self):
        return "<%s %.2f mm (%.2f inches)>" % (self.__class__.__name__, self.distance_mm, self.distance_inches)

    def __add__(self, other):
        if not isinstance(other, Distance):
            raise TypeError("Unsupported operand for + expected Distance")
        return distance_mm(self.distance_mm + other.distance_mm)

    def __sub__(self, other):
        if not isinstance(other, Distance):
            raise TypeError("Unsupported operand for - expected Distance")
        return distance_mm(self.distance_mm - other.distance_mm)

    def __mul__(self, other):
        if not isinstance(other, (int, float)):
            raise TypeError("Unsupported operand for * expected number")
        return distance_mm(self.distance_mm * other)

    def __truediv__(self, other):
        if not isinstance(other, (int, float)):
            raise TypeError("Unsupported operand for / expected number")
        return distance_mm(self.distance_mm / other)

    @property
    def distance_mm(self):  # pylint: disable=redefined-outer-name
        '''float: The distance in millimeters'''
        return self._distance_mm

    @property
    def distance_inches(self):  # pylint: disable=redefined-outer-name
        '''float: The distance in inches'''
        return self._distance_mm / 25.4


def distance_mm(distance_mm):  # pylint: disable=redefined-outer-name
    '''Returns an :class:`vector.util.Distance` instance set to the specified number of millimeters.'''
    return Distance(distance_mm=distance_mm)


def distance_inches(distance_inches):  # pylint: disable=redefined-outer-name
    '''Returns an :class:`vector.util.Distance` instance set to the specified number of inches.'''
    return Distance(distance_inches=distance_inches)


class Speed:
    '''Represents a speed.

    This class allows speeds to be measured  in millimeters per second.

    Use :func:`speed_mmps` convenience methods to generate
    a Speed instance.

    Args:
        speed_mmps (float): The number of millimeters per second the speed
            should represent.
    '''

    __slots__ = ('_speed_mmps')

    def __init__(self, speed_mmps=None):  # pylint: disable=redefined-outer-name
        if speed_mmps is None:
            raise ValueError("Expected speed_mmps keyword argument")
        self._speed_mmps = speed_mmps

    def __repr__(self):
        return "<%s %.2f mmps>" % (self.__class__.__name__, self.speed_mmps)

    def __add__(self, other):
        if not isinstance(other, Speed):
            raise TypeError("Unsupported operand for + expected Speed")
        return speed_mmps(self.speed_mmps + other.speed_mmps)

    def __sub__(self, other):
        if not isinstance(other, Speed):
            raise TypeError("Unsupported operand for - expected Speed")
        return speed_mmps(self.speed_mmps - other.speed_mmps)

    def __mul__(self, other):
        if not isinstance(other, (int, float)):
            raise TypeError("Unsupported operand for * expected number")
        return speed_mmps(self.speed_mmps * other)

    def __truediv__(self, other):
        if not isinstance(other, (int, float)):
            raise TypeError("Unsupported operand for / expected number")
        return speed_mmps(self.speed_mmps / other)

    @property
    def speed_mmps(self):  # pylint: disable=redefined-outer-name
        '''float: The speed in millimeters per second (mmps).'''
        return self._speed_mmps


def speed_mmps(speed_mmps):  # pylint: disable=redefined-outer-name
    '''Returns an :class:`vector.util.Speed` instance set to the specified millimeters per second speed'''
    return Speed(speed_mmps=speed_mmps)


class Component:
    def __init__(self, robot):
        self.logger = get_class_logger(__name__, self)
        self._robot = robot

    @property
    def robot(self):
        return self._robot

    @property
    def interface(self):
        return self._robot.conn.interface
