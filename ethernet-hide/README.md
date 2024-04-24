# Ethernet-hide (ehide)

Ethernet-hide (ehide) is a tool that hides the Ethernet interface on DUT but
still allows SSH'ing to it through that Ethernet interface. This tool can help
CrOS developers test in a no-network or WiFi-only environment with SSH
connections unaffected. This tool can also help protect SSH connection when
developers' behavior, such as restarting or deploying shill, can potentially
affect it. Ehide only runs on test images.

## Usage

On the DUT:

    ehide [options] <action>

For detailed descriptions of the options and actions, do:

    ehide -h

As starting or stopping ehide breaks current SSH connections, to start or stop
ehide on your DUT via SSH, we recommend to use inline SSH commands such as:

    ssh ${DUT} ehide start

rather than executing ehide in your interactive shell.
