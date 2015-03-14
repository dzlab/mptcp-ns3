# Project description #
The mptcp-ns3 project focuses on developing implementation of Multipath TCP on ns-3 for research purposes. The project implement the entire transport layer in ns-3.

Multipath TCP is an extension to TCP which aims to use multiple paths to handle a communication between two endpoints. MPTCP is the IETF working group to standardize Multipath TCP.

Please check the following URL for more information about multipath TCP. http://datatracker.ietf.org/wg/mptcp/charter/

# Current Status #

The current implementation is really close to the MPTCP specification:

  * MPTCP options: _MPC_ (Multipath Capable), _ADD_ and _REMOVE_ address, _JOIN_, etc.

  * Congestion Control: _Fully Coupled_, _Uncoupled TCPs_, _Linked Increases_, _RTT Compensator_.

  * Packet Reordering: None, Eifel, DSACK and F-RTO algorithms


# Getting Started #
Follow the instructions in the wiki page http://code.google.com/p/mptcp-ns3/wiki/MakeIt to successfully run simulations.

# References #
For more details on MPTCP NS-3 module please refer to http://code.google.com/p/mptcp-ns3/wiki/References

# Research Projects #
Research projects using MPTCP NS-3 http://code.google.com/p/mptcp-ns3/wiki/Projects

# Information #
Current version works for NS-3.6.
It should be made compatible with newer NS-3 versions.


Join MPTCP Google Group http://groups.google.com/group/mptcp/

Feel free to post your questions to mptcp@googlegroups.com