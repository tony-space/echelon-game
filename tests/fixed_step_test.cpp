#include <echelon/sim/fixed_step.hpp>

#include <boost/test/unit_test.hpp>

using ech::FixedStep;

BOOST_AUTO_TEST_SUITE(fixed_step)

BOOST_AUTO_TEST_CASE(hands_out_whole_steps)
{
	FixedStep fs(0.01f, 100);
	fs.advance(0.035f);
	int n = 0;
	while (fs.consume())
		++n;
	BOOST_TEST(n == 3);
	BOOST_TEST(fs.alpha() == 0.5f, boost::test_tools::tolerance(1e-3f));
}

BOOST_AUTO_TEST_CASE(carries_remainder_across_frames)
{
	FixedStep fs(0.01f, 100);
	fs.advance(0.006f);
	BOOST_TEST(!fs.consume());
	fs.advance(0.006f);
	BOOST_TEST(fs.consume());
	BOOST_TEST(!fs.consume());
}

BOOST_AUTO_TEST_CASE(drops_backlog_when_stalled)
{
	FixedStep fs(0.01f, 4);
	fs.advance(1.0f);
	int n = 0;
	while (fs.consume())
		++n;
	BOOST_TEST(n == 4);
	fs.advance(0.0f);
	BOOST_TEST(!fs.consume());
}

BOOST_AUTO_TEST_SUITE_END()
