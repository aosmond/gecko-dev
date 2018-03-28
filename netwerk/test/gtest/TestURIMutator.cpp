#include "gtest/gtest.h"
#include "mozilla/HashFunctions.h"
#include "nsCOMPtr.h"
#include "nsNetCID.h"
#include "nsIURL.h"
#include "nsIURIMutator.h"

using mozilla::HashString;

TEST(TestURIMutator, Mutator)
{
  nsAutoCString out;
  uint32_t outHash = 0;

  // This test instantiates a new nsStandardURL::Mutator (via contractID)
  // and uses it to create a new URI.
  nsCOMPtr<nsIURI> uri;
  nsresult rv = NS_MutateURI(NS_STANDARDURLMUTATOR_CONTRACTID)
                  .SetSpec(NS_LITERAL_CSTRING("http://example.com"))
                  .Finalize(uri);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(uri->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == NS_LITERAL_CSTRING("http://example.com/"));
  ASSERT_EQ(uri->GetSpecHash(true, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("http://example.com/"));

  // This test verifies that we can use NS_MutateURI to change a URI
  rv = NS_MutateURI(uri)
         .SetScheme(NS_LITERAL_CSTRING("ftp"))
         .SetHost(NS_LITERAL_CSTRING("mozilla.org"))
         .SetPathQueryRef(NS_LITERAL_CSTRING("/path?query#ref"))
         .Finalize(uri);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(uri->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == NS_LITERAL_CSTRING("ftp://mozilla.org/path?query#ref"));
  ASSERT_EQ(uri->GetSpecHash(true, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("ftp://mozilla.org/path?query#ref"));
  ASSERT_EQ(uri->GetSpecHash(false, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("ftp://mozilla.org/path?query"));

  // This test verifies that we can pass nsIURL to Finalize, and
  nsCOMPtr<nsIURL> url;
  rv = NS_MutateURI(uri)
         .SetScheme(NS_LITERAL_CSTRING("https"))
         .Finalize(url);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(url->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == NS_LITERAL_CSTRING("https://mozilla.org/path?query#ref"));
  ASSERT_EQ(url->GetSpecHash(true, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path?query#ref"));
  ASSERT_EQ(url->GetSpecHash(false, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path?query"));

  // This test verifies that we can pass nsIURL** to Finalize.
  // We need to use the explicit template because it's actually passing getter_AddRefs
  nsCOMPtr<nsIURL> url2;
  rv = NS_MutateURI(url)
         .SetRef(NS_LITERAL_CSTRING("newref"))
         .Finalize<nsIURL>(getter_AddRefs(url2));
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(url2->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == NS_LITERAL_CSTRING("https://mozilla.org/path?query#newref"));
  ASSERT_EQ(url2->GetSpecHash(true, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path?query#newref"));
  ASSERT_EQ(url2->GetSpecHash(false, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path?query"));

  // This test verifies that we can pass nsIURI** to Finalize.
  // No need to be explicit.
  auto functionSetRef = [](nsIURI* aURI, nsIURI** aResult) -> nsresult
  {
    return NS_MutateURI(aURI)
             .SetRef(NS_LITERAL_CSTRING("originalRef"))
             .Finalize(aResult);
  };

  nsCOMPtr<nsIURI> newURI;
  rv = functionSetRef(url2, getter_AddRefs(newURI));
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(newURI->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == NS_LITERAL_CSTRING("https://mozilla.org/path?query#originalRef"));
  ASSERT_EQ(newURI->GetSpecHash(true, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path?query#originalRef"));
  ASSERT_EQ(newURI->GetSpecHash(false, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path?query"));

  // This test verifies that we can pass nsIURI** to Finalize.
  // We need to use the explicit template because it's actually passing getter_AddRefs
  nsCOMPtr<nsIURI> uri2;
  rv = NS_MutateURI(url2)
         .SetQuery(NS_LITERAL_CSTRING("newquery"))
         .Finalize<nsIURI>(getter_AddRefs(uri2));
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(uri2->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == NS_LITERAL_CSTRING("https://mozilla.org/path?newquery#newref"));
  ASSERT_EQ(uri2->GetSpecHash(true, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path?newquery#newref"));
  ASSERT_EQ(uri2->GetSpecHash(false, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path?newquery"));

  // This test verifies that we can pass nsIURI** to Finalize.
  // No need to be explicit.
  auto functionSetQuery = [](nsIURI* aURI, nsIURL** aResult) -> nsresult
  {
    return NS_MutateURI(aURI)
             .SetQuery(NS_LITERAL_CSTRING("originalQuery"))
             .Finalize(aResult);
  };

  nsCOMPtr<nsIURL> newURL;
  rv = functionSetQuery(uri2, getter_AddRefs(newURL));
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(newURL->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == NS_LITERAL_CSTRING("https://mozilla.org/path?originalQuery#newref"));
  ASSERT_EQ(newURL->GetSpecHash(true, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path?originalQuery#newref"));
  ASSERT_EQ(newURL->GetSpecHash(false, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path?originalQuery"));

  // Check that calling Finalize twice will fail.
  NS_MutateURI mutator(newURL);
  rv = mutator.SetQuery(EmptyCString()).Finalize(uri2);
  ASSERT_EQ(rv, NS_OK);
  ASSERT_EQ(uri2->GetSpec(out), NS_OK);
  ASSERT_TRUE(out == NS_LITERAL_CSTRING("https://mozilla.org/path#newref"));
  ASSERT_EQ(uri2->GetSpecHash(true, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path#newref"));
  ASSERT_EQ(uri2->GetSpecHash(false, &outHash), NS_OK);
  ASSERT_EQ(outHash, HashString("https://mozilla.org/path"));
  nsCOMPtr<nsIURI> uri3;
  rv = mutator.Finalize(uri3);
  ASSERT_EQ(rv, NS_ERROR_NOT_AVAILABLE);
  ASSERT_TRUE(uri3 == nullptr);
}
