# Copyright (c) 2018 Anki, Inc.

'''
A synchronizer is used to make functions waitable outside
of the scope of the event loop. This allows for more
advanced use cases where multiple commands may be sent in
parallel (see :class:`AsyncRobot` in robot.py), but also
simpler use cases where everything executes synchronously.
'''

__all__ = ['Synchronizer']

import functools
import logging

import grpc

from . import exceptions

MODULE_LOGGER = logging.getLogger(__name__)


class Synchronizer:
    '''
    Class for managing asynchronous functions in a synchronous world
    '''

    def __init__(self, loop, remove_pending, func, *args, **kwargs):
        '''
        Create an Synchronizer
        '''
        self.remove_pending = remove_pending
        self.loop = loop
        self.task = self.loop.create_task(func(*args, **kwargs))

    def wait_for_completed(self):
        '''
        Wait until the task completes before continuing
        '''
        try:
            return self.loop.run_until_complete(self.task)
        finally:
            self.remove_pending(self)
        return None

    @staticmethod
    def disable_log(func):
        '''
        Use this decorator to disable the automatic debug logging of wrap

        TODO: Might be better to instead have this as a parameter you can pass to wrap
        '''
        func.disable_log = True
        return func

    @classmethod
    def wrap(cls, func):
        '''
        Decorator to wrap a function for synchronous usage
        '''

        @functools.wraps(func)
        def log_result(func, logger):
            if not hasattr(func, "disable_log"):
                async def log(*args, **kwargs):
                    result = None
                    try:
                        result = await func(*args, **kwargs)
                    except grpc.RpcError as rpc_error:
                        raise exceptions.connection_error(rpc_error) from rpc_error
                    logger.debug(f'{type(result)}: {str(result).strip()}')
                    return result
                return log
            return func

        @functools.wraps(func)
        def waitable(*args, **kwargs):
            '''
            Either returns an Synchronizer or finishes processing the function depending on if the
            object "is_async"
            '''
            that = args[0]
            log_wrapped_func = log_result(func, that.logger)

            # When invoking inside of a running event loop, things could explode.
            # Instead, we should return the async function and let users await it
            # manually.
            if that.robot.loop.is_running():
                return log_wrapped_func(*args, **kwargs)
            if that.robot.is_async:
                # Return a Synchronizer to manage Task completion
                synchronizer = cls(that.robot.loop,
                                   that.robot.remove_pending,
                                   log_wrapped_func,
                                   *args,
                                   **kwargs)
                that.robot.add_pending(synchronizer)
                return synchronizer
            return that.robot.loop.run_until_complete(log_wrapped_func(*args, **kwargs))
        return waitable
