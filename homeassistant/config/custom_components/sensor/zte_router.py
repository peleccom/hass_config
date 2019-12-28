"""
Sensor for ZTE router.
"""
import logging
import re
import voluptuous as vol
import homeassistant.helpers.config_validation as cv

from datetime import timedelta
from telnetlib import Telnet
from homeassistant.const import (CONF_NAME, CONF_DISPLAY_OPTIONS, CONF_HOST, CONF_PORT, CONF_USERNAME, CONF_PASSWORD)
from homeassistant.components.sensor import PLATFORM_SCHEMA
from homeassistant.helpers.entity import Entity
from homeassistant.util import Throttle

_LOGGER = logging.getLogger(__name__)

MIN_TIME_BETWEEN_UPDATES = timedelta(minutes=30)

DEFAULT_PORT = 23
DEFAULT_NAME = 'ZTERouter'
DEFAULT_TIMEOUT = 5

SENSOR_TYPES = {
    'uptime' : ['Uptime', None, 'mdi:clock'],
    'status' : ['Status', None, 'mdi:water'],
    'dl_speed' : ['Download speed', 'Kbps', 'mdi:download'],
    'ul_speed' : ['Upload speed', 'Kbps', 'mdi:upload'],
}

PLATFORM_SCHEMA = PLATFORM_SCHEMA.extend({
    vol.Required(CONF_HOST): cv.string,
    vol.Optional(CONF_PORT, default=DEFAULT_PORT): cv.port,
    vol.Optional(CONF_NAME, default=DEFAULT_NAME): cv.string,
    vol.Required(CONF_PASSWORD): cv.string,
    vol.Optional(CONF_DISPLAY_OPTIONS, default=list(SENSOR_TYPES.keys())):
        vol.All(cv.ensure_list, [vol.In(SENSOR_TYPES)]),
})

ICON = 'mdi:router-wireless'


def setup_platform(hass, config, add_devices, discovery_info=None):
    """Setup the sensor platform."""
    name = config.get(CONF_NAME)
    host = config.get(CONF_HOST)
    port = config.get(CONF_PORT)
    password = config.get(CONF_PASSWORD)

    data = ZTERouterData(host, port, password, config[CONF_DISPLAY_OPTIONS])
    devices = []

    for variable in config[CONF_DISPLAY_OPTIONS]:
        devices.append(ZTERouterSensor(config, data, variable))

    add_devices(devices)


class ZTERouterSensor(Entity):
    """Representation of a Sensor."""

    def __init__(self, config, data, sensor_types):
        """Initialize a HDDTemp sensor."""
        self.data = data
        self._name = '{0}_{1}'.format(config.get(CONF_NAME),
                                      SENSOR_TYPES[sensor_types][0])

        self._unit_of_measurement = SENSOR_TYPES[sensor_types][1]
        self.type = sensor_types
        self._state = None
        self.update()


    @property
    def name(self):
        """Return the name of the sensor."""
        return self._name

    @property
    def state(self):
        """Return the state of the sensor."""
        return self._state

    @property
    def unit_of_measurement(self):
        """Return the unit of measurement."""
        return SENSOR_TYPES[self.type][1] \
            if self.type in SENSOR_TYPES else None

    @property
    def icon(self):
        """Return the icon."""
        return SENSOR_TYPES[self.type][2] \
            if self.type in SENSOR_TYPES else None

    def update(self):
        """Get the latest data from router and update the states."""
        self.data.update()

        if self.data.status is  None:
            #self._state = "OK"
            _LOGGER.error('No Data Received')
            return
        else:
            if self.type == 'uptime':
                self._state = self.data.status.get('uptime')
            elif self.type == 'status':
                self._state = self.data.status.get('status')
            elif self.type == 'dl_speed':
                self._state = self.data.status.get('dl_speed')      
            elif self.type == 'ul_speed':
                self._state = self.data.status.get('ul_speed')


class ZTERouterData(object):
    """Get the latest data from modem and update the states."""

    ADSL_STATUS_REGEX = {
        "uptime": re.compile(r'ADSL uptime\s+(.*)'),
        "status": re.compile(r'current modem status:\s(.*)'),
        "dl_speed": re.compile(r'near-end interleaved channel bit rate:\s(.*)kbps'),
        "ul_speed": re.compile(r'far-end interleaved channel bit rate:\s(.*)kbps'),
    }

    def __init__(self, host, port, password, CONF_DISPLAY_OPTIONS):
        """Initialize the data object."""
        self.host = host
        self.port = port
        self.password = password
        self.options = CONF_DISPLAY_OPTIONS
        self.data = None

    @Throttle(MIN_TIME_BETWEEN_UPDATES)
    def update(self):
        """Connect to router via telnet to gather status."""
        try:
            self.status = dict()
            connection = Telnet(
                self.host, self.port, timeout=DEFAULT_TIMEOUT)
            connection.read_until(b"Password: ")
            connection.write((self.password + "\n").encode('ascii'))
            connection.read_until(b"ZTE>")

            if 'uptime' in self.options:
                connection.write(("show wan adsl uptime\n").encode('ascii'))
                connection.read_until(b"\n").decode('ascii')
                uptime = connection.read_until(b"\n").decode('ascii')
                match = re.match(self.ADSL_STATUS_REGEX['uptime'], uptime)
                if match:
                    group_str = match.group(1).strip()
                    self.status['uptime'] = group_str

            if 'status' in self.options:
                connection.write(("show wan adsl status\n").encode('ascii'))
                connection.read_until(b"\n").decode('ascii')
                uptime = connection.read_until(b"\n").decode('ascii')
                match = re.match(self.ADSL_STATUS_REGEX['status'], uptime)
                if match:
                    group_str = match.group(1).strip()
                    self.status['status'] = group_str

            if 'dl_speed' in self.options or 'ul_speed' in self.options:
                connection.write(("show wan adsl chandata\n").encode('ascii'))
                connection.read_until(b"\n").decode('ascii')
                uptime = connection.read_until(b"ZTE>").decode('ascii')

                match = re.search(self.ADSL_STATUS_REGEX['dl_speed'], uptime)
                if match:
                    group_str = match.group(1).strip()
                    self.status['dl_speed'] = group_str

                match = re.search(self.ADSL_STATUS_REGEX['ul_speed'], uptime)
                if match:
                    group_str = match.group(1).strip()
                    self.status['ul_speed'] = group_str



        except ConnectionRefusedError:
            _LOGGER.error('ZTE Router is not available at %s:%s', self.host, self.port)
            self.data = None
