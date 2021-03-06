#include "watcher.hpp"

#include "icemet/util/time.hpp"

#include <opencv2/imgcodecs.hpp>

#include <algorithm>
#include <stdexcept>
#include <vector>

Watcher::Watcher(Config* cfg) :
	Worker(COLOR_BRIGHT_CYAN "WATCHER" COLOR_RESET),
	m_cfg(cfg),
	m_prev(cv::makePtr<File>())
{
	m_log.info("Watching %s", m_cfg->paths.watch.string().c_str());
}

bool Watcher::init()
{
	m_filesOriginal = static_cast<FileQueue*>(m_outputs[0]->data);
	return true;
}

void Watcher::findFiles(std::queue<cv::Ptr<File>>& files)
{
	// Find files
	std::vector<cv::Ptr<File>> filesVec;
	auto iter = fs::recursive_directory_iterator(m_cfg->paths.watch);
	for (const auto& entry : iter) {
		if (!entry.is_regular_file())
			continue;
		
		// Check if the path is an ICEMET file
		fs::path path(entry.path());
		try {
			filesVec.push_back(cv::makePtr<File>(path));
		}
		catch (std::exception& e) {
			m_log.debug("Ignoring: '%s'", path.string().c_str());
		}
	}
	
	// Sort
	std::sort(filesVec.begin(), filesVec.end(), [](const auto& f1, const auto& f2) {
		return *f1 < *f2;
	});
	
	// Push to queue
	for (const auto& file : filesVec)
		files.push(file);
}

bool Watcher::loop()
{
	std::queue<cv::Ptr<File>> files;
	findFiles(files);
	while (!files.empty()) {
		cv::Ptr<File> file = files.front();
		
		// Check if the file is new
		if (*file > *m_prev) {
			Measure m;
			
			// Open the image
			std::string fn(file->path().string());
			cv::Mat mat = cv::imread(fn, cv::IMREAD_GRAYSCALE);
			if (!mat.data) {
				m_log.debug("Invalid image file: '%s'", fn.c_str());
				break;
			}
			mat.getUMat(cv::ACCESS_READ).copyTo(file->original);
			
			// Push to output queue
			file->setStatus(FILE_STATUS_NONE);
			m_log.debug("Opened %s (%.2f s)", file->name().c_str(), m.time());
			m_filesOriginal->push(file);
			m_prev = file;
		}
		files.pop();
	}
	
	if (!m_cfg->args.waitNew)
		return false;
	ssleep(1);
	return !m_outputs.empty();
}
