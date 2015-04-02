/**
 * Copyright (c) 2015 - The CM Authors <legal@clickmatcher.com>
 *   All Rights Reserved.
 *
 * This file is CONFIDENTIAL -- Distribution or duplication of this material or
 * the information contained herein is strictly forbidden unless prior written
 * permission is obtained.
 */
#include <algorithm>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include "fnord-base/io/fileutil.h"
#include "fnord-base/application.h"
#include "fnord-base/logging.h"
#include "fnord-base/cli/flagparser.h"
#include "fnord-base/util/SimpleRateLimit.h"
#include "fnord-base/InternMap.h"
#include "fnord-json/json.h"
#include "fnord-mdb/MDB.h"
#include "fnord-mdb/MDBUtil.h"
#include "fnord-sstable/sstablereader.h"
#include "fnord-sstable/sstablewriter.h"
#include "fnord-sstable/SSTableColumnSchema.h"
#include "fnord-sstable/SSTableColumnReader.h"
#include "fnord-sstable/SSTableColumnWriter.h"
#include "fnord-cstable/UInt32ColumnReader.h"
#include "fnord-cstable/UInt32ColumnWriter.h"
#include "fnord-cstable/BooleanColumnReader.h"
#include "fnord-cstable/BooleanColumnWriter.h"
#include "fnord-cstable/CSTableWriter.h"
#include "fnord-cstable/CSTableReader.h"
#include "common.h"
#include "CustomerNamespace.h"
#include "FeatureSchema.h"
#include "JoinedQuery.h"
#include "CTRCounter.h"
#include "analytics/AnalyticsQuery.h"
#include "analytics/CTRByPositionQuery.h"

using namespace fnord;


int main(int argc, const char** argv) {
  fnord::Application::init();
  fnord::Application::logToStderr();

  fnord::cli::FlagParser flags;

  flags.defineFlag(
      "input_file",
      fnord::cli::FlagParser::T_STRING,
      true,
      "i",
      NULL,
      "file",
      "<filename>");

  flags.defineFlag(
      "output_file",
      fnord::cli::FlagParser::T_STRING,
      true,
      "o",
      NULL,
      "file",
      "<filename>");

  flags.defineFlag(
      "loglevel",
      fnord::cli::FlagParser::T_STRING,
      false,
      NULL,
      "INFO",
      "loglevel",
      "<level>");

  flags.parseArgv(argc, argv);

  Logger::get()->setMinimumLogLevel(
      strToLogLevel(flags.getString("loglevel")));

  size_t debug_n = 0;
  size_t debug_z = 0;

  /* query level */
  cstable::UInt32ColumnWriter jq_page_col(1, 1);

  /* query item level */
  cstable::UInt32ColumnWriter position_col(2, 2, 100);
  cstable::BooleanColumnWriter clicked_col(2, 2);

  uint64_t r = 0;
  uint64_t n = 0;

  auto add_session = [&] (const cm::JoinedSession& sess) {
    ++n;

    for (const auto& q : sess.queries) {
      auto pg_str = cm::extractAttr(q.attrs, "pg");
      if (pg_str.isEmpty()) {
        jq_page_col.addNull(r, 1);
      } else {
        jq_page_col.addDatum(r, 1, std::stoul(pg_str.get()));
      }

      if (q.items.size() == 0) {
        if (r==0) ++debug_z;
        position_col.addNull(r, 1);
        clicked_col.addNull(r, 1);
      }

      for (const auto& i : q.items) {
        ++debug_n;
        if (r==0) ++debug_z;

        position_col.addDatum(r, 2, i.position);
        clicked_col.addDatum(r, 2, i.clicked);
        r = 2;
      }

      r = 1;
    }

    r = 0;
  };


  /* read input tables */
  int row_idx = 0;

  const auto& sstable = flags.getString("input_file");
  fnord::logInfo("cm.jqcolumnize", "Importing sstable: $0", sstable);

  /* read sstable header */
  sstable::SSTableReader reader(File::openFile(sstable, File::O_READ));

  if (reader.bodySize() == 0) {
    fnord::logCritical("cm.jqcolumnize", "unfinished sstable: $0", sstable);
    exit(1);
  }

  /* get sstable cursor */
  auto cursor = reader.getCursor();
  auto body_size = reader.bodySize();

  /* status line */
  util::SimpleRateLimitedFn status_line(kMicrosPerSecond, [&] () {
    fnord::logInfo(
        "cm.jqcolumnize",
        "[$0%] Reading sstable... rows=$1",
        (size_t) ((cursor->position() / (double) body_size) * 100),
        row_idx);
  });

  /* read sstable rows */
  for (; cursor->valid(); ++row_idx) {
    status_line.runMaybe();

    auto key = cursor->getKeyString();
    auto val = cursor->getDataBuffer();
    Option<cm::JoinedQuery> q;

    try {
      q = Some(json::fromJSON<cm::JoinedQuery>(val));
    } catch (const Exception& e) {
      fnord::logWarning("cm.jqcolumnize", e, "invalid json: $0", val.toString());
    }

    if (!q.isEmpty()) {
      cm::JoinedSession s;
      s.queries.emplace_back(q.get());
      add_session(s);
    }

    if (!cursor->next()) {
      break;
    }
  }

  status_line.runForce();

  cstable::CSTableWriter writer(flags.getString("output_file"), n);
  writer.addColumn("queries.items.position", &position_col);
  writer.addColumn("queries.items.clicked", &clicked_col);
  writer.commit();

  {
    cstable::CSTableReader reader(flags.getString("output_file"));

    auto t0 = WallClock::unixMicros();

    cm::AnalyticsQuery aq;
    cm::CTRByPositionQueryResult res;
    cm::CTRByPositionQuery q(&aq, &res);
    aq.scanTable(&reader);
    auto t1 = WallClock::unixMicros();
    fnord::iputs("scanned $0 rows in $1 ms", res.rows_scanned, (t1 - t0) / 1000.0f);
    for (const auto& p : res.counters) {
      fnord::iputs(
         "pos: $0, views: $1, clicks: $2, ctr: $3", 
          p.first, p.second.num_views,
          p.second.num_clicks,
          p.second.num_clicks / (double) p.second.num_views);
    }
  }

  return 0;
}

