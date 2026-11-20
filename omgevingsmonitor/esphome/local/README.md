# Local overrides (git-ignored)

Every `*.yaml` file in this folder is merged into `omgevingsmonitor.yaml` as an ESPHome
package. Use it for settings that belong to your network only, for example a static IP,
which shortens every report by skipping DHCP:

```yaml
# local/network.yaml
wifi:
  manual_ip:
    static_ip: 192.168.1.60
    gateway: 192.168.1.1
    subnet: 255.255.255.0
    dns1: 192.168.1.1
```

Pick an address outside your router's DHCP range, or reserve it for the device's MAC
address in the router, so no other device gets it.
