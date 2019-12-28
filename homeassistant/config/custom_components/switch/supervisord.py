"""
Switch for Supervisord process status.
"""
import logging
import xmlrpc.client

import voluptuous as vol

from homeassistant.components.switch import PLATFORM_SCHEMA
from homeassistant.const import CONF_URL
from homeassistant.helpers.entity import ToggleEntity
import homeassistant.helpers.config_validation as cv

_LOGGER = logging.getLogger(__name__)

ATTR_DESCRIPTION = 'description'
ATTR_GROUP = 'group'

DEFAULT_URL = 'http://localhost:9001/RPC2'

PLATFORM_SCHEMA = PLATFORM_SCHEMA.extend({
    vol.Optional(CONF_URL, default=DEFAULT_URL): cv.url,
})


def setup_platform(hass, config, add_devices, discovery_info=None):
    """Set up the Supervisord platform."""
    url = config.get(CONF_URL)
    try:
        supervisor_server = xmlrpc.client.ServerProxy(url)
        processes = supervisor_server.supervisor.getAllProcessInfo()
    except ConnectionRefusedError:
        _LOGGER.error("Could not connect to Supervisord")
        return False

    add_devices(
        [SupervisorProcessSwitch(info, supervisor_server)
         for info in processes], True)


class SupervisorProcessSwitch(ToggleEntity):
    """Representation of a Supervisord switch."""

    def __init__(self, info, server):
        """Initialize the switch."""
        self._info = info
        self._server = server
        self._available = True

    @property
    def name(self):
        """Return the name of the switch."""
        return self._info.get('name')

    @property
    def available(self):
        """Could the device be accessed during the last update call."""
        return self._available

    @property
    def is_on(self):
        """Return true if process is running."""
        state = self._info.get('statename')
        return state in ['RUNNING', 'RESTARTING']

    def turn_on(self, **kwargs):
        """Turn the process on."""
        self._server.supervisor.startProcess(self.name, True)

    def turn_off(self, **kwargs):
        """Turn the process off."""
        self._server.supervisor.stopProcess(self.name, True)


    def update(self):
        """Update process state."""
        try:
            self._info = self._server.supervisor.getProcessInfo(
                self.name)
            self._available = True
        except ConnectionRefusedError:
            _LOGGER.warning("Supervisord not available")
            self._available = False
