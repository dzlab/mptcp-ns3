# mptcp-ns3
## Project description
The mptcp-ns3 project focuses on developing implementation of Multipath TCP on ns-3 for research purposes. The project implement the entire transport layer in ns-3.

Multipath TCP is an extension to TCP which aims to use multiple paths to handle a communication between two endpoints. MPTCP is the IETF working group to standardize Multipath TCP.

Please check the following URL for more information about multipath TCP. http://datatracker.ietf.org/wg/mptcp/charter/

## Current Status
The current implementation is really close to the MPTCP specification:
- MPTCP options: MPC (Multipath Capable), ADD and REMOVE address, JOIN, etc.
- Congestion Control: Fully Coupled, Uncoupled TCPs, Linked Increases, RTT Compensator.
- Packet Reordering: None, Eifel, DSACK and F-RTO algorithms

## Getting Started
Follow the instructions in the wiki page https://github.com/dzlab/mptcp-ns3/blob/wiki/MakeIt.md to successfully run simulations.

## References
For more details on MPTCP NS-3 module please refer to https://github.com/dzlab/mptcp-ns3/blob/wiki/References.md

## Research Projects
Research projects using MPTCP NS-3 https://github.com/dzlab/mptcp-ns3/blob/wiki/Projects.md

## Information
Current version works for NS-3.6. It should be made compatible with newer NS-3 versions.

Join MPTCP Google Group http://groups.google.com/group/mptcp/

Feel free to post your questions to mptcp@googlegroups.com
