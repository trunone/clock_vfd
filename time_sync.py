#!/usr/bin/env python

from pymodbus.server.async import StartSerialServer
from pymodbus.datastore import ModbusSequentialDataBlock
from pymodbus.datastore import ModbusSlaveContext, ModbusServerContext
from pymodbus.transaction import ModbusRtuFramer, ModbusAsciiFramer

import time
import serial
import pymodbus
from twisted.internet.task import LoopingCall

import logging
logging.basicConfig()
log = logging.getLogger()
log.setLevel(logging.DEBUG)

def updating_writer(a):
    context = a[0]
    values = [
        (time.localtime()[5]),
        (time.localtime()[4]),
        (time.localtime()[3]),
        (time.localtime()[2]),
        (time.localtime()[6]),
        (time.localtime()[1]),
        (time.localtime()[0])%100]
    context[0].setValues(3, 0x1000, values)
    log.debug("Time Updated!")

def run_updating_server():
    store = ModbusSlaveContext(hr = ModbusSequentialDataBlock.create())
    context = ModbusServerContext(slaves=store, single=True)
    
    loop = LoopingCall(f=updating_writer, a=(context,))
    loop.start(1, now=True) # initially delay by time
    StartSerialServer(context, framer=ModbusRtuFramer, port='/dev/ttyACM0', timeout=0.3, baudrate=9600)

if __name__ == "__main__":
    run_updating_server()