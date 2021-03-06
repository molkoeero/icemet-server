#include "saver.hpp"

#include "icemet/util/time.hpp"

#include <opencv2/core.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <opencv2/icemet.hpp>

#include <queue>

Saver::Saver(Config* cfg, Database* db) :
	Worker(COLOR_BRIGHT_BLUE "SAVER" COLOR_RESET),
	m_cfg(cfg),
	m_db(db)
{
	m_log.info("Results %s", m_cfg->paths.results.string().c_str());
}

bool Saver::init()
{
	m_filesAnalysis = static_cast<FileQueue*>(m_inputs[0]->data);
	return true;
}

void Saver::move(const fs::path& src, const fs::path& dst) const
{
	if (fs::exists(dst))
		fs::remove(dst);
	try {
		fs::rename(src, dst);
	}
	catch (std::exception& e) {
		fs::copy(src, dst);
		fs::remove(src);
	}
}

void Saver::process(const FilePtr& file) const
{
	if ((file->status() == FILE_STATUS_EMPTY && !m_cfg->saves.empty) ||
	    (file->status() == FILE_STATUS_SKIP && !m_cfg->saves.skipped) ||
	    (file->status() == FILE_STATUS_NONE)) {
		fs::remove(file->path());
		return;
	}
	
	int n = file->particles.size();
	
	// Save files
	if (m_cfg->saves.original) {
		fs::create_directories(file->dir(m_cfg->paths.original));
		
		fs::path src(file->path());
		fs::path dst = file->path(m_cfg->paths.original, src.extension());
		move(src, dst);
	}
	else {
		fs::remove(file->path());
	}
	if (m_cfg->saves.preproc && !file->preproc.empty()) {
		fs::create_directories(file->dir(m_cfg->paths.preproc));
		
		fs::path dst(file->path(m_cfg->paths.preproc, m_cfg->types.results));
		cv::imwrite(dst.string(), file->preproc.getMat(cv::ACCESS_READ));
	}
	if (m_cfg->saves.recon && file->status() == FILE_STATUS_NOTEMPTY) {
		fs::create_directories(file->dir(m_cfg->paths.recon));
		
		for (int i = 0; i < n; i++) {
			fs::path dst(file->path(m_cfg->paths.recon, m_cfg->types.results, i+1));
			cv::imwrite(dst.string(), file->segments[i]->img);
		}
	}
	if (m_cfg->saves.threshold && file->status() == FILE_STATUS_NOTEMPTY) {
		fs::create_directories(file->dir(m_cfg->paths.threshold));
		
		for (int i = 0; i < n; i++) {
			fs::path dst(file->path(m_cfg->paths.threshold, m_cfg->types.results, i+1));
			cv::imwrite(dst.string(), file->particles[i]->img);
		}
	}
	if (m_cfg->saves.preview && file->status() == FILE_STATUS_NOTEMPTY) {
		fs::create_directories(file->dir(m_cfg->paths.preview));
		
		cv::Mat preview = cv::Mat::zeros(m_cfg->img.size, CV_8UC1);
		for (const auto& segm : file->segments) {
			// Invert
			cv::Mat imgInv;
			cv::bitwise_not(segm->img, imgInv);
			
			// Adjust
			cv::Mat imgTh, imgAdj;
			unsigned char th = cv::threshold(imgInv, imgTh, 0, 255, cv::THRESH_OTSU);
			cv::icemet::adjust(imgInv, imgAdj, th, 255, 0, 255);
			
			// Draw
			imgAdj.copyTo(cv::Mat(preview, segm->rect));
		}
		fs::path dst(file->path(m_cfg->paths.preview, m_cfg->types.lossy));
		cv::imwrite(dst.string(), preview);
	}
	
	// Write SQL
	for (int i = 0; i < n; i++) {
		const auto& segm = file->segments[i];
		const auto& par = file->particles[i];
		m_db->writeParticle({
			0, file->dt(),
			file->sensor(), file->frame(), (unsigned int)i+1,
			par->x, par->y, par->z,
			par->diam, par->diamCorr,
			par->circularity, par->dynRange, par->effPxSz,
			segm->rect
		});
	}
}

bool Saver::loop()
{
	// Collect files
	std::queue<FilePtr> files;
	m_filesAnalysis->collect(files);
	
	// Process
	while (!files.empty()) {
		FilePtr file = files.front();
		m_log.debug("Saving %s", file->name().c_str());
		Measure m;
		process(file);
		m_log.debug("Done %s (%.2f s)", file->name().c_str(), m.time());
		m_log.info("Done %s", file->name().c_str());
		files.pop();
	}
	msleep(1);
	return !m_inputs[0]->closed() || !m_filesAnalysis->empty();
}
