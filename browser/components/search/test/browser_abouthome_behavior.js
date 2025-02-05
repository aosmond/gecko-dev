/* Any copyright is dedicated to the Public Domain.
 * http://creativecommons.org/publicdomain/zero/1.0/ */

/*
 * Test home page search for all plugin URLs
 */

"use strict";

function test() {
  // Bug 992270: Ignore uncaught about:home exceptions (related to snippets from IndexedDB)
  ignoreAllUncaughtExceptions(true);

  let previouslySelectedEngine = Services.search.currentEngine;

  function replaceUrl(base) {
    return base;
  }

  let gMutationObserver = null;

  function verify_about_home_search(engine_name) {
    let engine = Services.search.getEngineByName(engine_name);
    ok(engine, engine_name + " is installed");

    Services.search.currentEngine = engine;

    // load about:home, but remove the listener first so it doesn't
    // get in the way
    gBrowser.removeProgressListener(listener);
    BrowserTestUtils.loadURI(gBrowser, "about:home");
    info("Waiting for about:home load");
    tab.linkedBrowser.addEventListener("load", function load(event) {
      if (event.originalTarget != tab.linkedBrowser.contentDocumentAsCPOW ||
          event.target.location.href == "about:blank") {
        info("skipping spurious load event");
        return;
      }
      tab.linkedBrowser.removeEventListener("load", load, true);

      // Observe page setup
      let doc = gBrowser.contentDocumentAsCPOW;
      gMutationObserver = new MutationObserver(function(mutations) {
        for (let mutation of mutations) {
          if (mutation.attributeName == "searchEngineName") {
            // Re-add the listener, and perform a search
            gBrowser.addProgressListener(listener);
            gMutationObserver.disconnect();
            gMutationObserver = null;
            executeSoon(function() {
              doc.getElementById("searchText").value = "foo";
              doc.getElementById("searchSubmit").click();
            });
          }
        }
      });
      gMutationObserver.observe(doc.documentElement, { attributes: true });
    }, true);
  }
  waitForExplicitFinish();

  let gCurrTest;
  let gTests = [
    {
      name: "Search with Bing from about:home",
      searchURL: replaceUrl("http://www.bing.com/search?q=foo&pc=MOZI&form=MOZSPG"),
      run() {
        verify_about_home_search("Bing");
      },
    },
    {
      name: "Search with Google from about:home",
      searchURL: replaceUrl("https://www.google.com/search?q=foo&ie=utf-8&oe=utf-8"),
      run() {
        verify_about_home_search("Google");
      },
    },
    {
      name: "Search with Amazon.com from about:home",
      searchURL: replaceUrl("https://www.amazon.com/exec/obidos/external-search/?field-keywords=foo&mode=blended&tag=mozilla-20&sourceid=Mozilla-search"),
      run() {
        verify_about_home_search("Amazon.com");
      },
    },
  ];

  function nextTest() {
    if (gTests.length) {
      gCurrTest = gTests.shift();
      info("Running : " + gCurrTest.name);
      executeSoon(gCurrTest.run);
    } else {
      // Make sure we listen again for uncaught exceptions in the next test or cleanup.
      executeSoon(finish);
    }
  }

  let tab = gBrowser.selectedTab = BrowserTestUtils.addTab(gBrowser);

  let listener = {
    onStateChange: function onStateChange(webProgress, req, flags, status) {
      info("onStateChange");
      // Only care about top-level document starts
      let docStart = Ci.nsIWebProgressListener.STATE_IS_DOCUMENT |
                     Ci.nsIWebProgressListener.STATE_START;
      if (!(flags & docStart) || !webProgress.isTopLevel)
        return;

      if (req.originalURI.spec == "about:blank")
        return;

      info("received document start");

      ok(req instanceof Ci.nsIChannel, "req is a channel");
      is(req.originalURI.spec, gCurrTest.searchURL, "search URL was loaded");
      info("Actual URI: " + req.URI.spec);

      req.cancel(Cr.NS_ERROR_FAILURE);

      executeSoon(nextTest);
    },
  };

  registerCleanupFunction(function() {
    Services.search.currentEngine = previouslySelectedEngine;
    gBrowser.removeProgressListener(listener);
    gBrowser.removeTab(tab);
    if (gMutationObserver)
      gMutationObserver.disconnect();
  });

  tab.linkedBrowser.addEventListener("load", function() {
    gBrowser.addProgressListener(listener);
    nextTest();
  }, {capture: true, once: true});
}
