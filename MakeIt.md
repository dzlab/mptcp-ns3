In order to build the code on ns-3 refer to the following steps:


  * Download ns-3 from http://www.nsnam.org/
  * Install ns-3 by following the instructions in http://www.nsnam.org/wiki/index.php/Installation
  * Download the mptcp-ns3 files from http://code.google.com/p/mptcp-ns3/downloads/list
  * Copy each downloaded file in the ns-3 subdirectory sepcified in **Summary + Labels** column which is located next to the **Filename** column
  * In each ns-3 directory, edit the file **wscript** and add the filename of the copied files to the appropriate place for _headers_ and _sources_

To run a simulation:
  * Launch a terminal
  * Add ns-3 directory to the _PATH_ environment's variable
  * To build and run a simulation use _./waf --run scratch/mpTopology_