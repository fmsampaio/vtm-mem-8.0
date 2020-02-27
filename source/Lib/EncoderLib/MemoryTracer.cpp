// Arthur
#include "MemoryTracer.h"


std::fstream MemoryTracer::fp;
bool MemoryTracer::firstOrRasterSearchFlag;

MemoryTracer::MemoryTracer() {
}

void MemoryTracer::init(std::string fileName, unsigned int wFrame, unsigned int hFrame, unsigned int searchRange) {
	fp.open(fileName.c_str(), std::fstream::out);
	firstOrRasterSearchFlag = false;
	
	std::string outStr = "";
	
	std::stringstream ss;
	ss << wFrame << " ";
	ss << hFrame << " ";
	ss << searchRange << std::endl;
	
	outStr += ss.str();
	fp << outStr;
}

void MemoryTracer::finalize() {
	fp.close();
}

void MemoryTracer::initFrame(int idCurrFrame) {
	std::string outStr = "I ";
	
	std::stringstream ss;
	ss << idCurrFrame << std::endl;
	outStr += ss.str();
	fp << outStr;
}

void MemoryTracer::initCTU(int xLCU, int yLCU) {
	std::string outStr = "L ";
	
	std::stringstream ss;
	ss << xLCU << " ";
	ss << yLCU << std::endl;
	
	outStr += ss.str();
	fp << outStr;
}

void MemoryTracer::initCU(int xCU, int yCU, int depthCU) {
	std::string outStr = "U ";
	
	std::stringstream ss;
	ss << xCU << " ";
	ss << yCU << " ";
	ss << depthCU << std::endl;
	
	outStr += ss.str();
	fp << outStr;
}

void MemoryTracer::initPU(int idPart, int sizePU, int idRefFrame) {
	std::string outStr = "P ";
	
	std::stringstream ss;
	ss << idPart << " ";
	ss << sizePU << " ";
	ss << idRefFrame << std::endl;
	
	outStr += ss.str();
	fp << outStr;
}

void MemoryTracer::insertFirstSearch(int xStart, int yStart) {
	std::string outStr = "F ";
	
	std::stringstream ss;
	ss << xStart << " ";
	ss << yStart << std::endl;
	
	outStr += ss.str();
	fp << outStr;
}

void MemoryTracer::insertCandidate(int xCand, int yCand) {
	std::string outStr = "C ";
	
	std::stringstream ss;
	ss << xCand << " ";
	ss << yCand << std::endl;
	
	outStr += ss.str();
	fp << outStr;
}

void MemoryTracer::insertRasterSearch(int xLeft, int xRight, int yTop, int yBottom) {
	std::string outStr = "R ";
	
	std::stringstream ss;
	ss << xLeft << " ";
	ss << xRight << " ";
	ss << yTop << " ";
	ss << yBottom << std::endl;
	
	outStr += ss.str();
	fp << outStr;
}

void MemoryTracer::finalizeCTU() {
	fp << "E" << std::endl;
}

void MemoryTracer::finalizePU() {
	fp << "p" << std::endl;
}

void MemoryTracer::finalizeCU() {
	fp << "u" << std::endl;
}

void MemoryTracer::finalizeFrame() {
	fp << "i" << std::endl;
}